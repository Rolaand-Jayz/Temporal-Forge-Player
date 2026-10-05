// ANVIL obs — ObservationSource implementation.
//
// Wraps the historical src/media Demuxer + VideoDecoder shell (reused in
// place, compiled via MediaReuseTus.cpp; reconciliation 3.2/3.6) into the
// successor Observation model, and implements the explicit
// decode -> working-space conversion boundary (reconciliation 3.8).
//
// Determinism (reconciliation 3.7): software decode is forced, no atomics, no
// RNG, fixed evaluation order in every output-affecting loop. libswscale is
// used single-threaded with explicitly set colorspace details — swscale's
// implicit defaults (limited-range 601) were a documented silent-drift point
// in the old pipeline and are never relied on here.
//
// Evidence-class labels: MEASURED FACT entries cite audit run dwfrun-015b2ece
// as recorded in docs/anvil/RECONCILIATION_20261005.md sections 2/3; PROJECT
// DECISION entries cite the task contract / reconciliation sections they
// implement.
#include "anvil/obs/ObservationSource.hpp"

#include "anvil/codecs/CodecSideInfo.hpp"
#include "media/Demuxer.hpp"
#include "media/VideoDecoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace anvil {
namespace {

// ---------------------------------------------------------------------------
// Colorspace mapping helpers (shared by plane normalization and
// convertToWorking)
// ---------------------------------------------------------------------------

// Kr/Kb per matrix coefficient. Table mirrors the established table in
// src/render/upload/YuvConstants.cpp:38-70 (the old pipeline's resolved
// YUV->RGB weights), including the legacy FCC and SMPTE240M variants and the
// unspecified-matrix fallback (height >= 720 ? 709 : 601, YuvConstants.cpp
// default case).
struct MatrixCoeffs {
  float kr = 0.2126f;
  float kb = 0.0722f;
  const char* name = "709";
};

MatrixCoeffs matrixFor(int avMatrix, int height, bool& assumed) {
  assumed = false;
  switch (avMatrix) {
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_RGB:  // YuvConstants treats RGB metadata as 709 weights
      return {0.2126f, 0.0722f, "709"};
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
      return {0.2990f, 0.1140f, "601"};
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      return {0.2627f, 0.0593f, "2020"};
    case AVCOL_SPC_FCC:
      return {0.3000f, 0.1100f, "fcc"};
    case AVCOL_SPC_SMPTE240M:
      return {0.2120f, 0.0870f, "smpte240m"};
    default:
      // Unspecified / exotic matrix: FFmpeg's common SD fallback, matching
      // YuvConstants.cpp:60-69. The assumption is RECORDED in
      // declaredSpaceOut, never silent (task contract).
      assumed = true;
      if (height >= 720) return {0.2126f, 0.0722f, "709"};
      return {0.2990f, 0.1140f, "601"};
  }
}

// SWS_CS_* constant for sws_getCoefficients, from the recorded matrix.
// Unspecified matrix falls back exactly like matrixFor (height-based 601/709).
int swsCoefficientsFor(int avMatrix, int height, bool& assumed) {
  assumed = false;
  switch (avMatrix) {
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_RGB:
      return SWS_CS_ITU709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
      return SWS_CS_ITU601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      return SWS_CS_BT2020;
    case AVCOL_SPC_FCC:
      return SWS_CS_FCC;
    case AVCOL_SPC_SMPTE240M:
      return SWS_CS_SMPTE240M;
    default:
      assumed = true;
      return height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
  }
}

// v1 transfer declaration. PROJECT DECISION (task contract / reconciliation
// 3.8): v1 does NOT linearize — the working space is display-referred and only
// the DECLARATION of the transfer is carried. Accepted declaration names are
// exactly "bt709" | "srgb" | "linear" | "pq" | "hlg"; any other SPECIFIED
// AVColorTransferCharacteristic is declared as "trc:<value>" — still declared
// (no silent assumption), but not silently relabeled as one of the five.
bool namedTransfer(int trc, std::string& out) {
  switch (trc) {
    case AVCOL_TRC_BT709: out = "bt709"; return true;
    case AVCOL_TRC_LINEAR: out = "linear"; return true;
    case AVCOL_TRC_IEC61966_2_1: out = "srgb"; return true;
    case AVCOL_TRC_SMPTE2084: out = "pq"; return true;
    case AVCOL_TRC_ARIB_STD_B67: out = "hlg"; return true;
    default:
      out = "trc:" + std::to_string(trc);
      return true;
  }
}

bool isAcceptedOverride(const std::string& name) {
  return name == "bt709" || name == "srgb" || name == "linear" ||
         name == "pq" || name == "hlg";
}

// Bilinear chroma sample at chroma-grid coordinate (tX, tY) with edge
// replication. Fixed evaluation order (determinism).
float sampleBilinear(const uint8_t* plane, int cw, int ch, float tX, float tY) {
  tX = std::clamp(tX, 0.0f, static_cast<float>(cw - 1));
  tY = std::clamp(tY, 0.0f, static_cast<float>(ch - 1));
  const int x0 = static_cast<int>(tX);
  const int y0 = static_cast<int>(tY);
  const int x1 = std::min(x0 + 1, cw - 1);
  const int y1 = std::min(y0 + 1, ch - 1);
  const float fx = tX - static_cast<float>(x0);
  const float fy = tY - static_cast<float>(y0);
  const float a = static_cast<float>(plane[y0 * cw + x0]);
  const float b = static_cast<float>(plane[y0 * cw + x1]);
  const float c = static_cast<float>(plane[y1 * cw + x0]);
  const float d = static_cast<float>(plane[y1 * cw + x1]);
  const float top = a + (b - a) * fx;
  const float bottom = c + (d - c) * fx;
  return top + (bottom - top) * fy;
}

}  // namespace

// ---------------------------------------------------------------------------
// FileObservationSource
// ---------------------------------------------------------------------------

class FileObservationSource::Impl {
 public:
  Impl(const std::string& path, bool requestCodecMv)
      : requestCodecMv_(requestCodecMv) {
    if (!demux_.open(path)) {
      lastError_ = "FileObservationSource: demuxer open failed for '" + path + "'";
      failed_ = true;
      return;
    }
    videoStreamIndex_ = demux_.info().videoIndex;
    if (videoStreamIndex_ < 0 || !demux_.info().video) {
      lastError_ = "FileObservationSource: no video stream in '" + path + "'";
      failed_ = true;
      return;
    }
    // avcodec_descriptor_get names are lowercase ("h264", "hevc", ...) —
    // the classifier's capability table expects exactly that.
    codecName_ = demux_.info().video->codecName;

    // PROJECT DECISION (reconciliation 3.7): the deterministic successor
    // decode path is software-only. Hardware frames would additionally carry
    // no host planes to copy and no motion side data (MEASURED FACT,
    // dwfrun-015b2ece: no hwaccel path exports MVs on this stack).
    decoder_.setSoftwareDecodeForced(true);
    decoder_.setThreadCount(1); // deterministic decode: fixed thread count (reconciliation 3.7)
    // requestCodecMv: the exact software+MVS mechanism the decoder already
    // provides — AV_CODEC_FLAG2_EXPORT_MVS is set unconditionally at open
    // (VideoDecoder.cpp), and setMotionMetadataRequested(true) forces the
    // software path so the side data actually survives.
    if (requestCodecMv_) decoder_.setMotionMetadataRequested(true);
    if (!decoder_.open(demux_.ctx(), videoStreamIndex_)) {
      lastError_ = "FileObservationSource: video decoder open failed for '" +
                   path + "'";
      failed_ = true;
      return;
    }
  }

  bool next(Observation& out) {
    if (failed_) return false;
    // spin guard: packets consumed without producing a frame inside ONE
    // next() call. A full B-frame reorder chain is far below this bound.
    int spins = 0;
    while (true) {
      temporal_forge::DecodedVideoFrame frame;
      if (decoder_.receiveFrame(frame)) {
        Observation obs;
        if (!buildObservation(frame, obs)) return false;  // failed_ set inside
        out = std::move(obs);
        return true;
      }
      if (decoder_.drainComplete()) return false;  // clean end of stream
      if (++spins > 4096) {
        lastError_ = "FileObservationSource: decode made no progress";
        failed_ = true;
        return false;
      }
      if (drainSent_) continue;  // draining: receiveFrame until EOF
      temporal_forge::Packet packet;
      if (!demux_.readPacket(packet)) {
        // EOF (or read error): hand the decoder a null packet so it drains
        // its delayed B-frames, then keep receiving until drainComplete.
        drainSent_ = true;
        decoder_.sendPacket(nullptr);
        continue;
      }
      if (packet.isFlush) {
        // Flush packets only appear after Demuxer::seekUs; a linear read of
        // this v1 source never triggers them, but handle honestly: a flush
        // resets decoder history and MUST bump the seek generation.
        decoder_.flush();
        ++generation_;
        continue;
      }
      if (packet.isEof) {
        drainSent_ = true;
        decoder_.sendPacket(nullptr);
        continue;
      }
      if (packet.streamIndex == videoStreamIndex_) {
        decoder_.sendPacket(packet.av);
      }
      // Non-video packets are skipped silently (audio/subtitle exist in
      // containers; they are not observations).
    }
  }

  uint32_t generation() const { return generation_; }
  const std::string& lastError() const { return lastError_; }

 private:
  bool buildObservation(const temporal_forge::DecodedVideoFrame& frame,
                        Observation& out) {
    out = {};
    // Color metadata FIRST: plane normalization derives its explicit
    // swscale colorspace details from it.
    out.color = mapColorMeta(frame);
    if (!normalizePlanes(frame, out)) return false;

    // ---- FrameIdentity ----
    // fileFrameIndex is OUR counter: it survives decoder flushes (the
    // decoder's own frameCounter_ resets on flush, VideoDecoder.cpp:459) and
    // is monotonic within one seek generation.
    out.id.fileFrameIndex = frameCounter_++;
    out.id.seekGeneration = generation_;
    // ptsUs from the decoder's existing conversion logic (pts or
    // best_effort_timestamp, stream-timebase -> microseconds).
    out.id.ptsUs = frame.ptsUs;
    // dtsUs from the raw packet DTS when present, else -1 (never fabricated).
    out.id.dtsUs = -1;
    if (frame.pktDtsTicks >= 0) {
      const AVRational tb = [&] {
        const temporal_forge::Timebase t = decoder_.timebase();
        return AVRational{t.num, t.den};
      }();
      if (tb.den != 0) {
        out.id.dtsUs = av_rescale_q(frame.pktDtsTicks, tb, AVRational{1, 1000000});
      }
    }
    // valid=false ONLY on decode-failure paths (corrupt / decode_error
    // marker from the decoder); planes may still be present but are marked.
    out.id.valid = !frame.corrupt;

    // ---- SideInfo ----
    out.side.bFrame = frame.bFrame;
    out.side.mvs.reserve(frame.motionVectors.size());
    for (const auto& mv : frame.motionVectors) {
      AnvilMv a;
      // MvEntry is already normalized to source pixels with the
      // current->previous convention (VideoDecoder.cpp:39-51) — straight
      // mapping, no sign or scale reinterpretation.
      a.dstX = mv.dstX;
      a.dstY = mv.dstY;
      a.w = mv.w;
      a.h = mv.h;
      a.mvX = mv.mvX;
      a.mvY = mv.mvY;
      // AVMotionVector.source: negative = past reference, positive = future
      // (MEASURED on libavcodec 63; bi-pred appears as two unlinked vectors).
      a.direction = mv.source < 0 ? int8_t{-1} : (mv.source > 0 ? int8_t{1} : int8_t{0});
      out.side.mvs.push_back(a);
    }
    // Provenance: the codec-MV channel is the source of the (possibly empty)
    // list whenever vectors were requested or actually appeared; coverage
    // below explains WHY the list is empty.
    if (!out.side.mvs.empty() || requestCodecMv_) {
      out.side.provenance = SideInfo::Provenance::CodecMv;
    }
    // Coverage via the pinned classifier (codecs worker implements it).
    // isIFrame marker: FFmpeg's keyframe flag (IDR/I pictures). A
    // non-keyframe I picture would degrade to "empty + Present" per the
    // classifier design — visible, not fabricated.
    const CodecSideInfoClassifier classifier(codecName_, frame.hwFrame);
    out.side.coverage = classifier.classify(out.side, frame.keyframe);
    return true;
  }

  static ColorMeta mapColorMeta(const temporal_forge::DecodedVideoFrame& f) {
    // VideoDecoder already resolved frame-vs-codec metadata
    // (VideoDecoder.cpp:277-296) but preserves AV-level "unspecified" when
    // both frame and codec are unspecified. Translate to ColorMeta's
    // preserved-unspecified convention (-1).
    ColorMeta c;
    c.matrixCoefficients =
        f.colorSpace == AVCOL_SPC_UNSPECIFIED ? -1 : f.colorSpace;
    c.primaries =
        f.colorPrimaries == AVCOL_PRI_UNSPECIFIED ? -1 : f.colorPrimaries;
    c.transfer =
        f.colorTransfer == AVCOL_TRC_UNSPECIFIED ? -1 : f.colorTransfer;
    c.range = f.colorRange == AVCOL_RANGE_JPEG
                  ? ColorMeta::Range::Full
                  : (f.colorRange == AVCOL_RANGE_MPEG
                         ? ColorMeta::Range::Limited
                         : ColorMeta::Range::Unspecified);
    c.chromaLocation =
        f.chromaLocation == AVCHROMA_LOC_UNSPECIFIED ? -1 : f.chromaLocation;
    // The planes carried by the Observation are always 8-bit 4:2:0 after
    // normalization (see normalizePlanes for the per-format-class record);
    // bitDepth therefore records the NORMALIZED depth, not the source depth.
    c.bitDepth = 8;
    return c;
  }

  bool normalizePlanes(const temporal_forge::DecodedVideoFrame& frame,
                       Observation& out) {
    const int w = frame.width;
    const int h = frame.height;
    if (w <= 0 || h <= 0) {
      lastError_ = "FileObservationSource: decoded frame has zero extent";
      failed_ = true;
      return false;
    }
    // Defensive: this source forces software decode; a hardware frame here
    // would have no host planes to deep-copy truthfully. Fail loudly rather
    // than guess (task contract).
    if (frame.hwFrame) {
      lastError_ = "FileObservationSource: hardware frame reached the "
                   "observation path; no host planes to copy";
      failed_ = true;
      return false;
    }
    if (frame.planes == 0) {
      lastError_ = "FileObservationSource: decoded frame without plane data";
      failed_ = true;
      return false;
    }

    SourcePlanes& planes = out.planes;
    planes.width = w;
    planes.height = h;
    const int cw = AV_CEIL_RSHIFT(w, 1);  // 4:2:0 chroma extent (AV convention)
    const int ch = AV_CEIL_RSHIFT(h, 1);

    const AVPixelFormat fmt = static_cast<AVPixelFormat>(frame.avFormat);
    if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P) {
      // ---- Class A: 8-bit planar 4:2:0 (YUV420P / deprecated YUVJ420P).
      // Pixel-exact deep copy: no resampling, no depth conversion, no
      // colorspace reinterpretation. bitDepth stays 8; matrix/range/
      // chromaLocation stay exactly as recorded from the decoder.
      if (frame.planes < 3) {
        lastError_ = "FileObservationSource: 4:2:0 frame with fewer than 3 planes";
        failed_ = true;
        return false;
      }
      planes.y.resize(static_cast<size_t>(w) * h);
      planes.u.resize(static_cast<size_t>(cw) * ch);
      planes.v.resize(static_cast<size_t>(cw) * ch);
      copyCompact(planes.y, frame.plane[0], frame.linesize[0], w, h);
      copyCompact(planes.u, frame.plane[1], frame.linesize[1], cw, ch);
      copyCompact(planes.v, frame.plane[2], frame.linesize[2], cw, ch);
      return true;
    }

    // ---- Classes B/C/D: normalize through libswscale with EXPLICIT
    // sws_setColorspaceDetails derived from the recorded ColorMeta. swscale
    // defaults (implicit limited-range 601) are a documented silent-drift
    // point in the old pipeline and are never relied on.
    //
    // Class B (high-bit-depth 4:2:0, e.g. yuv420p10le): depth truncated to 8
    // by the conversion (lossy quantization step, DOCUMENTED here; bitDepth
    // is recorded as 8). Chroma layout unchanged.
    // Class C (4:2:2 / 4:4:4 / gray): chroma subsampled to 4:2:0 with the
    // declared SWS_BILINEAR filter (lossy spatial step, DOCUMENTED).
    // Class D (RGB / GBRP): matrix conversion into YUV 4:2:0 using the
    // recorded matrix (or the recorded height-based fallback); the output is
    // full-range YUV, so the Observation's range metadata is updated to Full.
    //
    // Anything swscale cannot convert fails loudly via lastError_.
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    if (!desc) {
      lastError_ = "FileObservationSource: unknown pixel format " +
                   std::to_string(fmt);
      failed_ = true;
      return false;
    }
    const bool isRgb = (desc->flags & AV_PIX_FMT_FLAG_RGB) != 0;

    SwsContext* sws = sws_getContext(w, h, fmt, w, h, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
      // MEASURED-CONTRACT failure: this format class cannot be normalized
      // truthfully on this FFmpeg build — fail loudly rather than guess.
      lastError_ = std::string("FileObservationSource: libswscale cannot ") +
                   "normalize source format '" +
                   (av_get_pix_fmt_name(fmt) ? av_get_pix_fmt_name(fmt) : "?") +
                   "' to 8-bit 4:2:0";
      failed_ = true;
      return false;
    }

    bool matrixAssumed = false;
    const int cs = swsCoefficientsFor(out.color.matrixCoefficients, h, matrixAssumed);
    // Source range: YUV content uses the recorded range; an unspecified YUV
    // range is treated as limited — PROJECT DECISION (task contract), the
    // assumption is recorded in the Observation's own metadata path by the
    // convertToWorking boundary. RGB content is inherently full-scale.
    const int srcRange =
        isRgb ? 1 : (out.color.range == ColorMeta::Range::Full ? 1 : 0);
    const int dstRange = srcRange;  // preserve the source's declared range
    sws_setColorspaceDetails(sws, sws_getCoefficients(cs), srcRange,
                             sws_getCoefficients(cs), dstRange,
                             /*brightness=*/0, /*contrast=*/1 << 16,
                             /*saturation=*/1 << 16);

    std::vector<uint8_t> dstY(static_cast<size_t>(w) * h);
    std::vector<uint8_t> dstU(static_cast<size_t>(cw) * ch);
    std::vector<uint8_t> dstV(static_cast<size_t>(cw) * ch);
    uint8_t* dst[4] = {dstY.data(), dstU.data(), dstV.data(), nullptr};
    int dstStride[4] = {w, cw, cw, 0};

    const uint8_t* src[4] = {nullptr, nullptr, nullptr, nullptr};
    int srcStride[4] = {0, 0, 0, 0};
    for (int i = 0; i < 3 && i < frame.planes; ++i) {
      src[i] = frame.plane[i].data();
      srcStride[i] = frame.linesize[i];
    }
    const int outRows = sws_scale(sws, src, srcStride, 0, h, dst, dstStride);
    sws_freeContext(sws);
    if (outRows != h) {
      lastError_ = std::string("FileObservationSource: sws_scale failed (") +
                   "rows=" + std::to_string(outRows) + ")";
      failed_ = true;
      return false;
    }
    planes.y = std::move(dstY);
    planes.u = std::move(dstU);
    planes.v = std::move(dstV);
    if (isRgb) {
      // Class D output is full-range YUV; keep the metadata truthful.
      out.color.range = ColorMeta::Range::Full;
    }
    return true;
  }

  // Deep-copy a padded decoder plane row-by-row into a dense (width*height)
  // buffer, dropping the linesize padding. Fixed row order (determinism).
  static void copyCompact(std::vector<uint8_t>& dst,
                          const std::vector<uint8_t>& src, int linesize,
                          int width, int height) {
    dst.resize(static_cast<size_t>(width) * height);
    for (int row = 0; row < height; ++row) {
      const size_t srcOff = static_cast<size_t>(row) * linesize;
      std::memcpy(dst.data() + static_cast<size_t>(row) * width,
                  src.data() + srcOff, static_cast<size_t>(width));
    }
  }

  temporal_forge::Demuxer demux_;
  temporal_forge::VideoDecoder decoder_;
  std::string codecName_;
  int videoStreamIndex_ = -1;
  bool requestCodecMv_ = false;
  uint32_t generation_ = 0;    // seekGeneration; bumped on every decoder flush
  uint64_t frameCounter_ = 0;  // fileFrameIndex; survives decoder flushes
  bool drainSent_ = false;
  bool failed_ = false;
  std::string lastError_;
};

FileObservationSource::FileObservationSource(const std::string& path,
                                             bool requestCodecMv)
    : impl_(new Impl(path, requestCodecMv)) {}

FileObservationSource::~FileObservationSource() { delete impl_; }

bool FileObservationSource::next(Observation& out) { return impl_->next(out); }

uint32_t FileObservationSource::generation() const { return impl_->generation(); }

const std::string& FileObservationSource::lastError() const {
  return impl_->lastError();
}

// ---------------------------------------------------------------------------
// convertToWorking — the explicit decode -> working-space boundary
// (reconciliation 3.8; task contract items 5)
// ---------------------------------------------------------------------------

bool convertToWorking(const SourcePlanes& planes, const ColorMeta& color,
                      const std::string& transferOverride, WorkingImage& out,
                      std::string& errorOut, std::string& declaredSpaceOut) {
  out = WorkingImage{};
  errorOut.clear();
  declaredSpaceOut.clear();

  const int w = planes.width;
  const int h = planes.height;
  if (w <= 0 || h <= 0) {
    errorOut = "convertToWorking: empty planes";
    return false;
  }
  const size_t luma = static_cast<size_t>(w) * h;
  const int cw = AV_CEIL_RSHIFT(w, 1);
  const int ch = AV_CEIL_RSHIFT(h, 1);
  const size_t chroma = static_cast<size_t>(cw) * ch;
  if (planes.y.size() != luma || planes.u.size() != chroma ||
      planes.v.size() != chroma) {
    errorOut = "convertToWorking: plane sizes do not match 8-bit 4:2:0 extent";
    return false;
  }

  // ---- Matrix (Kr/Kb from the established YuvConstants table).
  bool matrixAssumed = false;
  const MatrixCoeffs mc = matrixFor(color.matrixCoefficients, h, matrixAssumed);
  const float kg = 1.0f - mc.kr - mc.kb;

  // ---- Range. Recorded range; PROJECT DECISION (task contract): an
  // unspecified range is treated as LIMITED for YUV content and the
  // assumption is recorded in declaredSpaceOut. Expand factors match
  // YuvConstants.cpp:73-75.
  const bool fullRange = color.range == ColorMeta::Range::Full;
  const bool rangeAssumed = color.range == ColorMeta::Range::Unspecified;
  const float yOffset = fullRange ? 0.0f : 16.0f / 255.0f;
  const float yScale = fullRange ? 1.0f : 255.0f / 219.0f;
  const float cScale = fullRange ? 1.0f : 255.0f / 224.0f;

  // ---- Transfer: declared, never linearized in v1 (PROJECT DECISION). An
  // unspecified transfer with an empty override is a HARD ERROR — unknown-
  // transfer content never silently enters reconstruction.
  std::string transfer;
  const char* transferSource = nullptr;
  if (color.transfer != -1) {
    namedTransfer(color.transfer, transfer);
    transferSource = "metadata";
  } else if (!transferOverride.empty()) {
    if (!isAcceptedOverride(transferOverride)) {
      errorOut = "convertToWorking: transferOverride '" + transferOverride +
                 "' is not one of bt709|srgb|linear|pq|hlg";
      return false;
    }
    transfer = transferOverride;
    transferSource = "override";
  } else {
    errorOut =
        "convertToWorking: color.transfer is unspecified and transferOverride "
        "is empty — refusing to assume a transfer silently";
    return false;
  }

  // ---- Chroma siting. 4:2:0 default is LEFT when the location is
  // unspecified (recorded assumption); CENTER when specified so. Vertical:
  // TOP/TOPLEFT are top-sited (phase 0); BOTTOM/BOTTOMLEFT are treated as
  // centered in v1 (HYPOTHESIS: negligible for 4:2:0 content; the deviation
  // is bounded by half a chroma row) — fixed, declared, not fabricated.
  float phaseX = 0.5f;
  float phaseY = 0.5f;
  switch (color.chromaLocation) {
    case AVCHROMA_LOC_LEFT:
    case AVCHROMA_LOC_TOPLEFT:
    case AVCHROMA_LOC_BOTTOMLEFT:
      phaseX = 0.0f;
      break;
    case AVCHROMA_LOC_CENTER:
    case AVCHROMA_LOC_TOP:
    case AVCHROMA_LOC_BOTTOM:
      phaseX = 0.5f;
      break;
    case AVCHROMA_LOC_UNSPECIFIED:
    default:
      phaseX = 0.0f;  // LEFT default for 4:2:0 (recorded)
      break;
  }
  if (color.chromaLocation == AVCHROMA_LOC_TOPLEFT ||
      color.chromaLocation == AVCHROMA_LOC_TOP) {
    phaseY = 0.0f;
  }
  const bool chromaAssumed = color.chromaLocation == -1;

  // ---- Chroma upsample: bilinear to the luma grid, honoring the siting.
  // Chroma sample j sits at luma coordinate 2j + phase; luma pixel x maps to
  // chroma coordinate (x - phase) / 2, edge-replicated.
  std::vector<float> cb(luma);
  std::vector<float> cr(luma);
  for (int y = 0; y < h; ++y) {
    const float tY = (static_cast<float>(y) - phaseY) * 0.5f;
    for (int x = 0; x < w; ++x) {
      const float tX = (static_cast<float>(x) - phaseX) * 0.5f;
      const float u = sampleBilinear(planes.u.data(), cw, ch, tX, tY);
      const float v = sampleBilinear(planes.v.data(), cw, ch, tX, tY);
      const size_t idx = static_cast<size_t>(y) * w + x;
      cb[idx] = (u / 255.0f - 0.5f) * cScale;
      cr[idx] = (v / 255.0f - 0.5f) * cScale;
    }
  }

  // ---- YUV -> RGB (planar float, clamped to the display-referred [0,1]).
  // Derivation identical to the YuvConstants push-constant construction
  // (YuvConstants.cpp:131-136): R = Y + 2(1-Kr)Cr; G = Y - 2Kb(1-Kb)/Kg Cb -
  // 2Kr(1-Kr)/Kg Cr; B = Y + 2(1-Kb)Cb.
  out.width = w;
  out.height = h;
  out.r.resize(luma);
  out.g.resize(luma);
  out.b.resize(luma);
  const float rCr = 2.0f * (1.0f - mc.kr);
  const float gCb = 2.0f * mc.kb * (1.0f - mc.kb) / kg;
  const float gCr = 2.0f * mc.kr * (1.0f - mc.kr) / kg;
  const float bCb = 2.0f * (1.0f - mc.kb);
  for (size_t i = 0; i < luma; ++i) {
    const float yy = (static_cast<float>(planes.y[i]) / 255.0f - yOffset) * yScale;
    float r = yy + rCr * cr[i];
    float g = yy - gCb * cb[i] - gCr * cr[i];
    float b = yy + bCb * cb[i];
    out.r[i] = std::clamp(r, 0.0f, 1.0f);
    out.g[i] = std::clamp(g, 0.0f, 1.0f);
    out.b[i] = std::clamp(b, 0.0f, 1.0f);
  }

  // ---- Recorded declaration of the space this conversion produced.
  declaredSpaceOut =
      std::string("working=display_referred_rgb_f32") +
      ";matrix=" + mc.name +
      ";matrixAssumed=" + (matrixAssumed ? "1" : "0") +
      ";range=" + (fullRange ? "full" : "limited") +
      ";rangeAssumed=" + (rangeAssumed ? "1" : "0") +
      ";chroma=" + (chromaAssumed ? "left(default)" : "recorded") +
      ";transfer=" + transfer +
      ";transferSource=" + transferSource +
      ";eotf=none(v1)";
  return true;
}

}  // namespace anvil
