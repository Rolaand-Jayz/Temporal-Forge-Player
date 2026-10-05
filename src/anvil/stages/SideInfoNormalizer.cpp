// ANVIL stage: side-info normalization (reconciliation 3.5, ask item 1).
// passthrough: returns the input unchanged.
// densify_blocks: densifies the CENTER frame's codec SideInfo into a grid
// MotionField prior via anvil::BlockDensifier (src=Codec per that contract);
// the raw side info is preserved alongside.
#include "anvil/stages/Stages.hpp"
#include "anvil/codecs/CodecSideInfo.hpp"
#include "anvil/stages/StageInternal.hpp"
#include "anvil/stages/StageUtil.hpp"

#include <memory>

namespace anvil {
namespace {

// v1 grid geometry: gridStep 8, working dims == center plane dims (1:1).
constexpr int kGridStep = 8;

class PassthroughSideInfoNormalizer final : public SideInfoNormalizer {
 public:
  std::string name() const override { return "passthrough"; }
  NormalizedSideInfo run(const FrameWindow& /*window*/,
                         const NormalizedSideInfo& in) override {
    return in;
  }
};

class DensifyBlocksNormalizer final : public SideInfoNormalizer {
 public:
  std::string name() const override { return "densify_blocks"; }
  NormalizedSideInfo run(const FrameWindow& window,
                         const NormalizedSideInfo& in) override {
    NormalizedSideInfo out = in;
    const Observation& center = window.center();
    const int w = center.planes.width;
    const int h = center.planes.height;
    if (w > 0 && h > 0) {
      // 1:1 v1: source pixel dims == working dims, so the densifier's source
      // geometry equals the working grid. BlockDensifier labels its output
      // src=Codec (CodecSideInfo.hpp contract) — honest provenance, never
      // relabeled as estimator output.
      const BlockDensifier densifier(kGridStep, w, h);
      out.densifiedPrior = std::make_shared<MotionField>(
          densifier.densify(center.side.mvs, w, h));
    }
    // Raw side information survives: the center's unmodified SideInfo is
    // carried in .side (guardrail — reconciliation 3.9).
    out.side = center.side;
    return out;
  }
};

}  // namespace

std::unique_ptr<SideInfoNormalizer> makePassthroughNormalizer() {
  return std::make_unique<PassthroughSideInfoNormalizer>();
}

std::unique_ptr<SideInfoNormalizer> makeDensifyBlocksNormalizer() {
  return std::make_unique<DensifyBlocksNormalizer>();
}

}  // namespace anvil
