// ANVIL stage: residual refinement (reconciliation 3.5, ask item 3).
// none:       returns the seeds unchanged (null bypass).
// hillclimb2: per neighbor, per block — up to 2 rounds of SSD hill climbing
//             over the 4 axis neighbors (+-1px, fixed order up/down/left/
//             right), then a 1-D parabolic subpixel fit per axis. Evaluated
//             on 8-bit luma planes (the only signal the stage contract
//             provides); deterministic, no RNG.
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

constexpr int kGridStep = 8;

// Vertex of the parabola through (x-1, sM1), (x, s0), (x+1, sP1), clamped to
// +-0.5px. Flat or concave-down denominators carry no subpixel evidence -> 0.
float parabolaOffset(std::uint64_t sM1, std::uint64_t s0, std::uint64_t sP1) {
  const double a = static_cast<double>(sM1);
  const double b = static_cast<double>(s0);
  const double c = static_cast<double>(sP1);
  const double denom = a - 2.0 * b + c;
  if (denom <= 0.0) return 0.0f;
  const double off = 0.5 * (a - c) / denom;
  return static_cast<float>(std::clamp(off, -0.5, 0.5));
}

class NoneRefiner final : public ResidualRefiner {
 public:
  std::string name() const override { return "none"; }
  std::vector<MotionField> run(const FrameWindow& /*window*/,
                               const std::vector<MotionField>& seeds) override {
    return seeds;
  }
};

class HillClimb2Refiner final : public ResidualRefiner {
 public:
  std::string name() const override { return "hillclimb2"; }
  std::vector<MotionField> run(const FrameWindow& window,
                               const std::vector<MotionField>& seeds) override {
    const Observation& center = window.center();
    const int w = center.planes.width;
    const int h = center.planes.height;
    const std::vector<uint8_t>& centerLuma = center.planes.y;
    const auto neighbors = stages::neighborsPastThenFuture(window);

    std::vector<MotionField> out;
    out.reserve(seeds.size());
    for (size_t i = 0; i < seeds.size() && i < neighbors.size(); ++i) {
      MotionField f = seeds[i];
      const int gs = f.gridStep > 0 ? f.gridStep : kGridStep;
      const std::vector<uint8_t>& neighborLuma = neighbors[i]->planes.y;
      for (BlockVec& bv : f.blocks) {
        const int bx = bv.gx * gs;
        const int by = bv.gy * gs;
        const int bw = std::min(gs, w - bx);
        const int bh = std::min(gs, h - by);
        if (bw <= 0 || bh <= 0) continue;
        auto ssdAt = [&](int dx, int dy) {
          return stages::lumaBlockSsd(centerLuma, neighborLuma, w, h,
                                      bx, by, bw, bh, dx, dy);
        };
        // Refinement operates on the integer luma lattice; the seed is
        // rounded to its nearest integer start (HYPOTHESIS: acceptable for
        // v1 — the subpixel fit below re-derives the fraction).
        int cx = static_cast<int>(std::lround(bv.mv.x));
        int cy = static_cast<int>(std::lround(bv.mv.y));
        std::uint64_t cur = ssdAt(cx, cy);
        // Up to 2 rounds; each round evaluates the 4 axis neighbors in fixed
        // order (up, down, left, right) and moves to the best strictly
        // improving one (improvement > 1e-4 == any strict decrease on
        // integer SSDs).
        static constexpr int kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int round = 0; round < 2; ++round) {
          std::uint64_t best = cur;
          int bestX = cx, bestY = cy;
          for (const auto& d : kDirs) {
            const std::uint64_t s = ssdAt(cx + d[0], cy + d[1]);
            if (s < best) {
              best = s;
              bestX = cx + d[0];
              bestY = cy + d[1];
            }
          }
          if (best < cur) {
            cur = best;
            cx = bestX;
            cy = bestY;
          } else {
            break;  // no strictly improving neighbor: stable
          }
        }
        // 1-D parabola per axis on SSD(current-1), SSD(current), SSD(current+1).
        const float offX = parabolaOffset(ssdAt(cx - 1, cy), cur, ssdAt(cx + 1, cy));
        const float offY = parabolaOffset(ssdAt(cx, cy - 1), cur, ssdAt(cx, cy + 1));
        bv.mv = Vec2f{static_cast<float>(cx) + offX, static_cast<float>(cy) + offY};
        // Quality stays the seed's declared value: the refiner moves the
        // position, it does not re-label coarse coverage/quality.
      }
      out.push_back(std::move(f));
    }
    // Seed/neighbor count mismatch (defensive): pass any remaining seeds
    // through unchanged rather than inventing refinement inputs.
    for (size_t i = neighbors.size(); i < seeds.size(); ++i) {
      out.push_back(seeds[i]);
    }
    return out;
  }
};

}  // namespace

std::unique_ptr<ResidualRefiner> makeNoneRefiner() {
  return std::make_unique<NoneRefiner>();
}

std::unique_ptr<ResidualRefiner> makeHillClimb2Refiner() {
  return std::make_unique<HillClimb2Refiner>();
}

}  // namespace anvil
