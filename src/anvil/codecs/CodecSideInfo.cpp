// ANVIL codec side-information classifier + block densifier (reconciliation 3.9).
// Implementation of the pinned CodecSideInfo.hpp contract.
//
// Evidence classes:
//   - MEASURED FACT entries cite the target-stack audit run (dwfrun-015b2ece,
//     FFmpeg n9.0.2 / libavcodec 63.1.102, Navi31/Mesa 26.2.4) as recorded in
//     docs/anvil/RECONCILIATION_20261005.md section 2 and 3.9.
//   - PROJECT DECISION entries cite the reconciliation sections they implement.
#include "anvil/codecs/CodecSideInfo.hpp"

#include <utility>

namespace anvil {

CodecSideInfoClassifier::CodecSideInfoClassifier(std::string codecName, bool hwDecode) {
  capability_.codecName = std::move(codecName);
  capability_.hwDecodePath = hwDecode;
  // MEASURED FACT (FFmpeg n9.0.2 / libavcodec 63.1.102, audit run dwfrun-015b2ece,
  // reconciliation 3.9): on this stack ONLY H.264 software decode exports motion
  // vectors (AV_CODEC_FLAG2_EXPORT_MVS + AVMotionVector side data on P/B frames).
  // HEVC software decode does not export; AV1 software decode does not export
  // (libdav1d absent); VAAPI and Vulkan hardware decode do not export on
  // Navi31/Mesa 26.2.4. Any codec NOT in this table is treated conservatively as
  // non-exporting until measured otherwise.
  capability_.codecMvExport = (capability_.codecName == "h264");
}

SideInfo::Coverage CodecSideInfoClassifier::classify(const SideInfo& side, bool isIFrame) const {
  // Hardware decode: MEASURED FACT (dwfrun-015b2ece) — no hwaccel path exports
  // motion vectors, regardless of codec table entry.
  if (capability_.hwDecodePath) {
    return SideInfo::Coverage::HwDecodeNoExport;
  }
  // Codec without measured export support on this stack (hevc/av1/unknown).
  if (!capability_.codecMvExport) {
    return SideInfo::Coverage::UnsupportedCodec;
  }
  // Export-capable software codec from here on. Non-empty mvs is unambiguous.
  if (!side.mvs.empty()) {
    return SideInfo::Coverage::Present;
  }
  // I frames emit no side data at all on this stack (MEASURED FACT,
  // dwfrun-015b2ece), so an I frame with empty mvs is intra by design.
  if (isIFrame) {
    return SideInfo::Coverage::IntraFrame;
  }
  // PROJECT DECISION (task contract, Types.hpp SideInfo model — no fifth state):
  // empty mvs on a NON-I frame of an export-capable codec must NOT be labeled
  // IntraFrame — that would fabricate an intra claim. The truthful state is
  // "codec emitted no vectors this frame"; consumers treat empty+Present as
  // exactly that. Ambiguity stays visible instead of being resolved by invention.
  return SideInfo::Coverage::Present;
}

BlockDensifier::BlockDensifier(int gridStep, int srcWidth, int srcHeight)
    : gridStep_(gridStep), srcWidth_(srcWidth), srcHeight_(srcHeight) {}

MotionField BlockDensifier::densify(const std::vector<AnvilMv>& mvs,
                                    int workingWidth, int workingHeight) const {
  MotionField field;
  field.src = MotionField::Src::Codec;
  field.gridStep = gridStep_;
  // Ceil division so right/bottom edge cells exist even when the working
  // dimensions are not multiples of gridStep; every cell is classified
  // (covered or explicit no-coverage), never dropped.
  field.gridWidth = (workingWidth + gridStep_ - 1) / gridStep_;
  field.gridHeight = (workingHeight + gridStep_ - 1) / gridStep_;
  field.blocks.reserve(static_cast<size_t>(field.gridWidth) *
                       static_cast<size_t>(field.gridHeight));

  // Deterministic single pass, row-major (gy outer, gx inner): blocks[i] has
  // gx = i % gridWidth, gy = i / gridWidth — a MotionField invariant.
  for (int gy = 0; gy < field.gridHeight; ++gy) {
    for (int gx = 0; gx < field.gridWidth; ++gx) {
      BlockVec cell;
      cell.gx = gx;
      cell.gy = gy;

      // v1 is 1:1 scale (PROJECT DECISION): source coords equal working coords,
      // so the cell center in working space is the same point in source space.
      const int cx = gx * gridStep_ + gridStep_ / 2;
      const int cy = gy * gridStep_ + gridStep_ / 2;

      // Nearest-block by containment of the cell CENTER; no cross-block
      // smoothing or blending anywhere — discontinuities between codec blocks
      // are preserved by construction (guardrail, EVIDENCE_GUARDRAILS.md /
      // reconciliation 3.9). Codec blocks do not overlap; if malformed input
      // ever overlaps, first match in mvs order wins — a fixed evaluation
      // order, so the result stays deterministic.
      const AnvilMv* hit = nullptr;
      if (cx >= 0 && cx < srcWidth_ && cy >= 0 && cy < srcHeight_) {
        for (const AnvilMv& b : mvs) {
          if (cx >= b.dstX && cx < b.dstX + static_cast<int>(b.w) &&
              cy >= b.dstY && cy < b.dstY + static_cast<int>(b.h)) {
            hit = &b;
            break;
          }
        }
      }

      if (hit != nullptr) {
        cell.mv = Vec2f{hit->mvX, hit->mvY};
        cell.quality = 1.0f;
      } else {
        // Explicit no-coverage: zero motion, quality 0. Never a fabricated or
        // interpolated vector (reconciliation 3.9: no field is fabricated).
        cell.mv = Vec2f{0.0f, 0.0f};
        cell.quality = 0.0f;
      }
      field.blocks.push_back(cell);
    }
  }
  // fwbwError stays empty: the codec API exposes no forward-backward residual,
  // and inventing one is forbidden (Types.hpp AnvilMv no-fabrication rule).
  return field;
}

}  // namespace anvil
