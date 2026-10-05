// ANVIL obs module tests (owned by the obs implementation worker).
//
// (a) convertToWorking numeric contract: known YUV constants -> expected RGB
//     within 2/255; full vs limited range divergence; 601 vs 709 divergence on
//     a chroma-heavy patch; unspecified-transfer hard-error path; override
//     path records the declared space.
// (b) Deterministic smoke over the real demux/decode shell: IF the ffmpeg CLI
//     is present, generate a tiny deterministic clip (lavfi testsrc2, 64x48,
//     6 frames, libx264, yuv420p, fixed CRF) and verify FileObservationSource
//     yields 6 observations with monotonic ptsUs and correctly sized planes;
//     otherwise SKIP with a printed note and exit 0 (exit-77 semantics).
#include "anvil/obs/ObservationSource.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <libavutil/pixfmt.h>

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what.c_str());
    ++g_failures;
  }
}

void expectNear(float got, float want, float tol, const std::string& what) {
  if (!(std::fabs(got - want) <= tol)) {
    std::printf("FAIL: %s got=%.6f want=%.6f (tol %.6f)\n", what.c_str(), got,
                want, tol);
    ++g_failures;
  }
}

constexpr float kTol = 2.0f / 255.0f;

// Build a constant 8-bit 4:2:0 patch (even dimensions).
anvil::SourcePlanes makeConstantPlanes(int w, int h, uint8_t y, uint8_t u,
                                       uint8_t v) {
  anvil::SourcePlanes p;
  p.width = w;
  p.height = h;
  p.y.assign(static_cast<size_t>(w) * h, y);
  const int cw = (w + 1) / 2;
  const int ch = (h + 1) / 2;
  p.u.assign(static_cast<size_t>(cw) * ch, u);
  p.v.assign(static_cast<size_t>(cw) * ch, v);
  return p;
}

anvil::ColorMeta makeColor(int matrix, anvil::ColorMeta::Range range,
                           int transfer, int chromaLocation) {
  anvil::ColorMeta c;
  c.matrixCoefficients = matrix;
  c.primaries = -1;
  c.transfer = transfer;
  c.range = range;
  c.chromaLocation = chromaLocation;
  c.bitDepth = 8;
  return c;
}

// ---- (a) convertToWorking ----

void testNeutralLimited709() {
  // Limited-range neutral gray: Y=235 -> 1.0, Y=16 -> 0.0, U=V=128.
  anvil::ColorMeta color =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Limited,
                AVCOL_TRC_BT709, AVCHROMA_LOC_LEFT);
  anvil::WorkingImage out;
  std::string err, declared;

  anvil::SourcePlanes white = makeConstantPlanes(4, 4, 235, 128, 128);
  expect(anvil::convertToWorking(white, color, "", out, err, declared),
         "neutral 709 limited converts");
  expectNear(out.r[0], 1.0f, kTol, "709 limited Y=235 -> R");
  expectNear(out.g[5], 1.0f, kTol, "709 limited Y=235 -> G");
  expectNear(out.b[15], 1.0f, kTol, "709 limited Y=235 -> B");
  expect(declared.find("range=limited") != std::string::npos,
         "declared space records limited range");
  expect(declared.find("transferSource=metadata") != std::string::npos,
         "declared space records metadata transfer source");
  expect(declared.find("eotf=none") != std::string::npos,
         "declared space records no EOTF (v1)");

  anvil::SourcePlanes black = makeConstantPlanes(4, 4, 16, 128, 128);
  expect(anvil::convertToWorking(black, color, "", out, err, declared),
         "black converts");
  expectNear(out.r[0], 0.0f, kTol, "709 limited Y=16 -> R");
}

void testFullVsLimited() {
  // Same code values, different recorded range -> different output, and each
  // matches its own expected constant.
  const anvil::ColorMeta full =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Full, AVCOL_TRC_BT709,
                AVCHROMA_LOC_LEFT);
  const anvil::ColorMeta limited =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Limited,
                AVCOL_TRC_BT709, AVCHROMA_LOC_LEFT);
  anvil::WorkingImage f, l;
  std::string err, declared;
  const anvil::SourcePlanes planes = makeConstantPlanes(4, 4, 235, 128, 128);
  expect(anvil::convertToWorking(planes, full, "", f, err, declared),
         "full converts");
  expect(anvil::convertToWorking(planes, limited, "", l, err, declared),
         "limited converts");
  expectNear(f.r[0], 235.0f / 255.0f, kTol, "full Y=235 -> 235/255");
  expectNear(l.r[0], 1.0f, kTol, "limited Y=235 -> 1.0");
  expect(std::fabs(f.r[0] - l.r[0]) > 4.0f / 255.0f,
         "full vs limited differ materially");
  expect(declared.find("rangeAssumed=0") != std::string::npos,
         "recorded range is not an assumption");
}

void test601Vs709ChromaHeavy() {
  // Chroma-heavy patch: Y=128, U=64, V=192 (limited). Expected values from
  // the Kr/Kb table (YuvConstants.cpp:38-70) with the pinned formulas.
  const anvil::ColorMeta c601 =
      makeColor(AVCOL_SPC_SMPTE170M, anvil::ColorMeta::Range::Limited,
                AVCOL_TRC_SMPTE170M, AVCHROMA_LOC_LEFT);
  const anvil::ColorMeta c709 =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Limited,
                AVCOL_TRC_BT709, AVCHROMA_LOC_LEFT);
  const anvil::SourcePlanes planes = makeConstantPlanes(4, 4, 128, 64, 192);
  anvil::WorkingImage r601, r709;
  std::string err, declared;
  expect(anvil::convertToWorking(planes, c601, "", r601, err, declared),
         "601 converts");
  expect(declared.find("matrix=601") != std::string::npos,
         "601 recorded in declared space");
  expect(anvil::convertToWorking(planes, c709, "", r709, err, declared),
         "709 converts");
  expect(declared.find("matrix=709") != std::string::npos,
         "709 recorded in declared space");

  expectNear(r601.r[0], 0.911991f, kTol, "601 R on chroma-heavy patch");
  expectNear(r709.r[0], 0.961363f, kTol, "709 R on chroma-heavy patch");
  expectNear(r601.g[0], 0.405708f, kTol, "601 G on chroma-heavy patch");
  expectNear(r709.g[0], 0.431192f, kTol, "709 G on chroma-heavy patch");
  expect(std::fabs(r601.r[0] - r709.r[0]) > 4.0f / 255.0f,
         "601 vs 709 R differ materially on chroma-heavy patch");
}

void testUnspecifiedTransferErrorPath() {
  const anvil::ColorMeta color =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Limited,
                /*transfer=*/-1, AVCHROMA_LOC_LEFT);
  const anvil::SourcePlanes planes = makeConstantPlanes(4, 4, 128, 128, 128);
  anvil::WorkingImage out;
  std::string err, declared;
  const bool ok = anvil::convertToWorking(planes, color, "", out, err, declared);
  expect(!ok, "unspecified transfer + empty override fails");
  expect(!err.empty(), "error text present");
  expect(declared.empty(), "no declared space on failure");
  expect(out.r.empty(), "no output on failure");
}

void testOverrideRecordsDeclaredSpace() {
  const anvil::ColorMeta color =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Limited,
                /*transfer=*/-1, AVCHROMA_LOC_LEFT);
  const anvil::SourcePlanes planes = makeConstantPlanes(4, 4, 235, 128, 128);
  anvil::WorkingImage out;
  std::string err, declared;
  expect(anvil::convertToWorking(planes, color, "bt709", out, err, declared),
         "override path converts");
  expect(declared.find("transfer=bt709") != std::string::npos,
         "override transfer recorded");
  expect(declared.find("transferSource=override") != std::string::npos,
         "override source recorded");

  std::string err2, declared2;
  expect(!anvil::convertToWorking(planes, color, "vlog", out, err2, declared2),
         "invalid override rejected");
  expect(!err2.empty(), "invalid override error text");
}

void testUnspecifiedMatrixFallback() {
  // Unspecified matrix on a small frame -> 601 fallback, recorded.
  const anvil::ColorMeta color =
      makeColor(/*matrix=*/-1, anvil::ColorMeta::Range::Limited,
                AVCOL_TRC_BT709, AVCHROMA_LOC_LEFT);
  const anvil::SourcePlanes planes = makeConstantPlanes(4, 4, 128, 64, 192);
  anvil::WorkingImage out;
  std::string err, declared;
  expect(anvil::convertToWorking(planes, color, "", out, err, declared),
         "unspecified-matrix converts with fallback");
  expect(declared.find("matrixAssumed=1") != std::string::npos,
         "matrix fallback recorded as assumed");
  expect(declared.find("matrix=601") != std::string::npos,
         "small frame falls back to 601");
}

void testBadPlanesRejected() {
  const anvil::ColorMeta color =
      makeColor(AVCOL_SPC_BT709, anvil::ColorMeta::Range::Limited,
                AVCOL_TRC_BT709, AVCHROMA_LOC_LEFT);
  anvil::SourcePlanes broken = makeConstantPlanes(4, 4, 128, 128, 128);
  broken.u.pop_back();  // wrong chroma extent
  anvil::WorkingImage out;
  std::string err, declared;
  expect(!anvil::convertToWorking(broken, color, "", out, err, declared),
         "inconsistent planes rejected");
}

// ---- (b) deterministic smoke over the real demux/decode shell ----

bool ffmpegCliPresent() {
  return std::system("ffmpeg -version >/dev/null 2>&1") == 0;
}

void testFileObservationSourceSmoke() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "anvil_obs_smoke";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  expect(!ec, "smoke temp dir created");
  if (ec) return;
  const std::string clip = (dir / "smoke.mp4").string();
  // Deterministic generation: fixed geometry, fixed frame count, fixed CRF,
  // single-threaded encoder. No RNG involved (lavfi testsrc2 is procedural).
  std::string cmd =
      "ffmpeg -hide_banner -loglevel error -y "
      "-f lavfi -i testsrc2=size=64x48:rate=8 "
      "-frames:v 6 -c:v libx264 -crf 24 -pix_fmt yuv420p "
      "-x264-params threads=1:keyint=6:min-keyint=6:ref=2 "
      "-flags +bitexact -fflags +bitexact \"" +
      clip + "\" >/dev/null 2>&1";
  if (std::system(cmd.c_str()) != 0 || !fs::exists(clip)) {
    // Environment gap (no libx264 in the CLI build), not a code failure —
    // SKIP semantics like the media_pipeline fixture test.
    std::printf("SKIP: ffmpeg CLI present but libx264 clip generation "
                "failed; smoke not run\n");
    return;
  }

  anvil::FileObservationSource source(clip, /*requestCodecMv=*/true);
  std::vector<anvil::Observation> obs;
  anvil::Observation o;
  while (source.next(o)) obs.push_back(o);
  expect(source.lastError().empty() || obs.size() == 6,
         "no unexpected source error: " + source.lastError());
  expect(obs.size() == 6, "expected 6 observations, got " +
                              std::to_string(obs.size()));
  if (obs.size() != 6) return;

  int64_t prevPts = -1;
  for (size_t i = 0; i < obs.size(); ++i) {
    expect(obs[i].id.fileFrameIndex == i,
           "fileFrameIndex monotonic at " + std::to_string(i));
    expect(obs[i].id.seekGeneration == 0, "generation 0 in linear read");
    expect(obs[i].id.ptsUs >= prevPts, "ptsUs monotonic at frame " +
                                            std::to_string(i));
    prevPts = obs[i].id.ptsUs;
    expect(obs[i].planes.width == 64 && obs[i].planes.height == 48,
           "plane extent 64x48");
    expect(obs[i].planes.y.size() == 64u * 48u, "Y plane size");
    expect(obs[i].planes.u.size() == 32u * 24u, "U plane size");
    expect(obs[i].planes.v.size() == 32u * 24u, "V plane size");
    expect(obs[i].color.bitDepth == 8, "normalized bitDepth 8");
    expect(obs[i].id.valid, "frame valid");
    expect(obs[i].id.dtsUs >= 0 || obs[i].id.dtsUs == -1,
           "dtsUs is a timestamp or -1");
  }
  expect(source.generation() == 0, "generation stays 0 without flush");
  // First frame is the IDR keyframe of the fixed-keyint clip: H.264 software
  // decode emits no MV side data on I frames (MEASURED FACT, dwfrun-015b2ece)
  // so the pinned classifier must say IntraFrame, not Present-with-secret.
  expect(obs[0].side.coverage == anvil::SideInfo::Coverage::IntraFrame,
         "I frame coverage is IntraFrame");
  expect(!obs[0].side.bFrame, "I frame not marked B");
  // dtsUs is present for mp4 content (packets carry DTS).
  expect(obs[0].id.dtsUs >= 0, "dtsUs recorded for mp4 input");
  // Working-space boundary on real data: unspecified transfer must refuse,
  // the declared override must pass.
  anvil::WorkingImage img;
  std::string err, declared;
  expect(!anvil::convertToWorking(obs[0].planes, obs[0].color, "", img, err,
                                  declared),
         "real clip with unspecified transfer refuses silent conversion");
  expect(anvil::convertToWorking(obs[0].planes, obs[0].color, "bt709", img, err,
                                 declared),
         "real clip converts under declared override");
  expect(img.width == 64 && img.height == 48, "working image extent");

  // End of stream is stable: further next() calls keep returning false.
  expect(!source.next(o), "stream stays ended");
  std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main() {
  testNeutralLimited709();
  testFullVsLimited();
  test601Vs709ChromaHeavy();
  testUnspecifiedTransferErrorPath();
  testOverrideRecordsDeclaredSpace();
  testUnspecifiedMatrixFallback();
  testBadPlanesRejected();

  if (!ffmpegCliPresent()) {
    std::printf("SKIP: ffmpeg CLI not present; FileObservationSource smoke "
                "not run (exit 77 semantics: returning 0 with note)\n");
  } else {
    testFileObservationSourceSmoke();
  }

  if (g_failures == 0) {
    std::printf("anvil_obs_tests: ALL PASS\n");
    return 0;
  }
  std::printf("anvil_obs_tests: %d failure(s)\n", g_failures);
  return 1;
}
