// ANVIL codec side-information capability + adapters (reconciliation 3.9).
// Capability cells are MEASURED on the target stack (audit dwfrun-015b2ece):
// H.264 software decode exports AVMotionVectors; HEVC and AV1 do not export
// them in FFmpeg n9.0.2; no hardware path (VAAPI/Vulkan) exports them.
// Capability is detected at runtime from the actual decoder, never assumed.
#pragma once

#include "anvil/core/Types.hpp"

#include <string>
#include <vector>

namespace anvil {

struct CodecCapability {
  std::string codecName;          // "h264", "hevc", "av1", ...
  bool codecMvExport = false;     // measured/probed export support
  bool hwDecodePath = false;      // true when frames came through a hwaccel
};

// Classifies a decoded frame's side-information state. Implemented over the
// Observation's SideInfo + codec identity; never fabricates fields.
// - I frames (no vectors emitted by design) -> Coverage::IntraFrame
// - codecs with no export support on this stack -> Coverage::UnsupportedCodec
// - hw-decoded frames -> Coverage::HwDecodeNoExport
// - else, with vectors -> Coverage::Present
class CodecSideInfoClassifier {
 public:
  // codecName: lowercase AVCodecID name; hwDecode: frame came from hwaccel.
  explicit CodecSideInfoClassifier(std::string codecName, bool hwDecode);
  SideInfo::Coverage classify(const SideInfo& side, bool isIFrame) const;
  const CodecCapability& capability() const { return capability_; }

 private:
  CodecCapability capability_;
};

// Side-info normalizer: densify block MVs into a grid MotionField prior that
// preserves discontinuities (nearest-block assignment; no cross-block
// blending/smoothing — guardrail from EVIDENCE_GUARDRAILS.md). Cells without
// codec coverage get zero motion with quality 0 — never fabricated coverage.
// Raw side info remains in SideInfo; the dense prior is additional.
class BlockDensifier {
 public:
  // gridStep: working-pixel block size of the output MotionField grid.
  // srcWidth/srcHeight: source pixel dimensions of the MV blocks.
  BlockDensifier(int gridStep, int srcWidth, int srcHeight);
  // Produces a MotionField with src=Codec over the working grid.
  MotionField densify(const std::vector<AnvilMv>& mvs, int workingWidth, int workingHeight) const;

 private:
  int gridStep_;
  int srcWidth_, srcHeight_;
};

}  // namespace anvil
