// ANVIL stage: visibility estimation (reconciliation 3.5, ask item 4).
// unity:       weight 1 everywhere, nothing occluded (bypass/control).
// photo_fwbw:  photometric residual weight with forward-backward occlusion.
//              The stage contract (Stages.hpp) hands the estimator only the
//              FrameWindow, so the photometric signal is the normalized luma
//              plane (y/255) of center vs neighbor — the working-space RGB
//              images do not exist at this pipeline point. Residual
//              constants are therefore in [0,1] normalized units.
#include "anvil/stages/Stages.hpp"
#include "anvil/stages/StageInternal.hpp"
#include "anvil/stages/StageUtil.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace anvil {
namespace {

// PROJECT DECISION (ask item 4): sigma = 0.08 and fwbw occlusion threshold
// 0.35 are documented v1 constants, not tuned values.
constexpr double kSigma = 0.08;
constexpr double kFwbwOcclusion = 0.35;

class UnityVisibility final : public VisibilityEstimator {
 public:
  std::string name() const override { return "unity"; }
  std::vector<Visibility> run(const FrameWindow& window,
                              const std::vector<MotionField>& /*motion*/) override {
    const Observation& center = window.center();
    const size_t n = stages::neighborsPastThenFuture(window).size();
    const size_t npix = static_cast<size_t>(center.planes.width) *
                        static_cast<size_t>(center.planes.height);
    std::vector<Visibility> out(n);
    for (Visibility& v : out) {
      v.src = Visibility::Src::Estimated;
      v.width = center.planes.width;
      v.height = center.planes.height;
      v.weight.assign(npix, 1.0f);
      v.occluded.assign(npix, 0);  // 0 = visible (Types.hpp)
    }
    return out;
  }
};

class PhotoFwBwVisibility final : public VisibilityEstimator {
 public:
  std::string name() const override { return "photo_fwbw"; }
  std::vector<Visibility> run(const FrameWindow& window,
                              const std::vector<MotionField>& motion) override {
    const Observation& center = window.center();
    const int w = center.planes.width;
    const int h = center.planes.height;
    const std::vector<uint8_t>& centerLuma = center.planes.y;
    const auto neighbors = stages::neighborsPastThenFuture(window);
    const size_t npix = static_cast<size_t>(w) * static_cast<size_t>(h);

    std::vector<Visibility> out;
    out.reserve(neighbors.size());
    for (size_t i = 0; i < neighbors.size(); ++i) {
      const MotionField empty{};
      const MotionField& mf = i < motion.size() ? motion[i] : empty;
      const int gs = mf.gridStep > 0 ? mf.gridStep : 8;
      // fwbwError is per-block; treat the slot as populated only when its
      // size matches the declared grid (a later stage/worker populates it —
      // ask item 4; otherwise the occlusion condition is skipped).
      const bool haveFwbw =
          !mf.fwbwError.empty() &&
          mf.fwbwError.size() ==
              static_cast<size_t>(mf.gridWidth) * static_cast<size_t>(mf.gridHeight);
      const std::vector<uint8_t>& neighborLuma = neighbors[i]->planes.y;

      Visibility v;
      v.src = Visibility::Src::Estimated;
      v.width = w;
      v.height = h;
      v.weight.assign(npix, 0.0f);
      v.occluded.assign(npix, 0);
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          const size_t p = static_cast<size_t>(y) * static_cast<size_t>(w) + x;
          const int gx = x / gs;
          const int gy = y / gs;
          Vec2f mv{};
          if (!mf.blocks.empty()) {
            const int bidx = gy * mf.gridWidth + gx;
            if (bidx >= 0 && bidx < static_cast<int>(mf.blocks.size())) {
              mv = mf.blocks[static_cast<size_t>(bidx)].mv;
            }
          }
          const float s = stages::bilinearSampleU8(
              neighborLuma, w, h, static_cast<float>(x) + mv.x,
              static_cast<float>(y) + mv.y);
          const float r =
              std::abs(static_cast<float>(centerLuma[p]) - s) / 255.0f;
          const double t = static_cast<double>(r) / kSigma;
          float wt = static_cast<float>(std::exp(-t * t));
          wt = std::clamp(wt, 0.0f, 1.0f);
          if (haveFwbw) {
            const size_t b = static_cast<size_t>(gy) * static_cast<size_t>(mf.gridWidth) +
                             static_cast<size_t>(gx);
            if (mf.fwbwError[b] > kFwbwOcclusion) {
              v.occluded[p] = 1;
              wt = 0.0f;
            }
          }
          v.weight[p] = wt;
        }
      }
      out.push_back(std::move(v));
    }
    return out;
  }
};

}  // namespace

std::unique_ptr<VisibilityEstimator> makeUnityVisibility() {
  return std::make_unique<UnityVisibility>();
}

std::unique_ptr<VisibilityEstimator> makePhotoFwBwVisibility() {
  return std::make_unique<PhotoFwBwVisibility>();
}

}  // namespace anvil
