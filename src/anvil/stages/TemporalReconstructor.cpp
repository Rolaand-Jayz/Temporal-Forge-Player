// ANVIL stage: temporal reconstruction (reconciliation 3.5, ask item 6).
// center:         single-frame control — out = center image.
// weighted_blend: confidence-weighted aligned blend over the window,
//                 out(p) = (center(p) + sum_i w_i(p) * sample(N_i, p + mv_i))
//                           / (1 + sum_i w_i(p)).
// Determinism: per-pixel accumulation happens in float with neighbors visited
// in fixed index order (past then future) — the summation is a single fixed
// sequence, never reassociated (PROJECT DECISION: float accumulate + fixed
// order for bitwise reproducibility at 1:1 v1).
#include "anvil/stages/Stages.hpp"
#include "anvil/stages/StageInternal.hpp"
#include "anvil/stages/StageUtil.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace anvil {
namespace {

class CenterReconstructor final : public TemporalReconstructor {
 public:
  std::string name() const override { return "center"; }
  ReconstructionResult run(const FrameWindow& window,
                           const StageBundle& bundle) override {
    ReconstructionResult r;
    r.fileFrameIndex = window.center().id.fileFrameIndex;
    r.ptsUs = window.center().id.ptsUs;
    r.out = bundle.center;
    r.geo = bundle.geometry;
    // Ablation: center only.
    r.ablation.usedFrameIndices.push_back(window.center().id.fileFrameIndex);
    return r;
  }
};

class WeightedBlendReconstructor final : public TemporalReconstructor {
 public:
  std::string name() const override { return "weighted_blend"; }
  ReconstructionResult run(const FrameWindow& window,
                           const StageBundle& bundle) override {
    ReconstructionResult r;
    r.fileFrameIndex = window.center().id.fileFrameIndex;
    r.ptsUs = window.center().id.ptsUs;
    r.geo = bundle.geometry;

    const WorkingImage& center = bundle.center;
    const int w = center.width;
    const int h = center.height;
    const size_t npix = static_cast<size_t>(w) * static_cast<size_t>(h);
    r.out.width = w;
    r.out.height = h;
    r.out.r.assign(npix, 0.0f);
    r.out.g.assign(npix, 0.0f);
    r.out.b.assign(npix, 0.0f);

    const size_t n = bundle.neighbors.size();
    // Precompute per neighbor whether it contributes anywhere (any pixel
    // with weight > 0, fixed scan). Ablation uses exactly this predicate.
    std::vector<char> anyWeight(n, 0);
    for (size_t i = 0; i < n; ++i) {
      if (i >= bundle.neighborVisibility.size()) continue;
      for (float wt : bundle.neighborVisibility[i].weight) {
        if (wt > 0.0f) {
          anyWeight[i] = 1;
          break;
        }
      }
    }

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const size_t p = static_cast<size_t>(y) * static_cast<size_t>(w) + x;
        // The center always contributes 1 — zero total weight is impossible.
        float acc[3] = {center.r[p], center.g[p], center.b[p]};
        float wsum = 1.0f;
        for (size_t i = 0; i < n; ++i) {
          if (anyWeight[i] == 0) continue;
          if (i >= bundle.neighborVisibility.size() ||
              i >= bundle.neighborMotion.size() ||
              i >= bundle.neighbors.size()) {
            continue;  // misaligned bundle: defensive skip, contributes 0
          }
          const Visibility& vis = bundle.neighborVisibility[i];
          if (p >= vis.occluded.size() || vis.occluded[p] != 0) continue;
          const float wt = p < vis.weight.size() ? vis.weight[p] : 0.0f;
          if (wt <= 0.0f) continue;

          const MotionField& mf = bundle.neighborMotion[i];
          const int gs = mf.gridStep > 0 ? mf.gridStep : 8;
          Vec2f mv{};
          if (!mf.blocks.empty()) {
            const int bidx = (y / gs) * mf.gridWidth + (x / gs);
            if (bidx >= 0 && bidx < static_cast<int>(mf.blocks.size())) {
              mv = mf.blocks[static_cast<size_t>(bidx)].mv;
            }
          }
          const WorkingImage& nb = bundle.neighbors[i];
          const float sx = static_cast<float>(x) + mv.x;
          const float sy = static_cast<float>(y) + mv.y;
          acc[0] += wt * stages::bilinearSampleF(nb.r, nb.width, nb.height, sx, sy);
          acc[1] += wt * stages::bilinearSampleF(nb.g, nb.width, nb.height, sx, sy);
          acc[2] += wt * stages::bilinearSampleF(nb.b, nb.width, nb.height, sx, sy);
          wsum += wt;
        }
        r.out.r[p] = acc[0] / wsum;
        r.out.g[p] = acc[1] / wsum;
        r.out.b[p] = acc[2] / wsum;
      }
    }

    // Ablation: neighbors with a nonzero total weight contribution; dropped
    // is empty — the runner drops neighbors before reconstruct (ask item 6).
    for (size_t i = 0; i < n; ++i) {
      if (anyWeight[i] == 0) continue;
      if (i < bundle.neighborFrameIndices.size()) {
        r.ablation.usedFrameIndices.push_back(bundle.neighborFrameIndices[i]);
      }
    }
    return r;
  }
};

}  // namespace

std::unique_ptr<TemporalReconstructor> makeCenterReconstructor() {
  return std::make_unique<CenterReconstructor>();
}

std::unique_ptr<TemporalReconstructor> makeWeightedBlendReconstructor() {
  return std::make_unique<WeightedBlendReconstructor>();
}

}  // namespace anvil
