// ANVIL stage: coarse correspondence (reconciliation 3.5, ask item 2).
// zero:        the zero-motion control (all blocks zero, quality 0, Estimated).
// codec:       rasterize the CENTER's SideInfo.mvs onto the per-neighbor grid,
//              direction-filtered per neighbor side (MEASURED: H.264
//              export_mvs yields past-reference vectors; AnvilMv.direction is
//              -1 past / +1 future per the API comment).
// blocksearch: deterministic full 8x8 luma block search, dx,dy in [-4..4].
// Per-neighbor output arrays are index-aligned past-then-future.
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
constexpr int kSearchRadius = 4;

class ZeroCoarse final : public CoarseCorrespondence {
 public:
  std::string name() const override { return "zero"; }
  std::vector<MotionField> run(const FrameWindow& window,
                               const NormalizedSideInfo& /*side*/) override {
    const Observation& center = window.center();
    const auto neighbors = stages::neighborsPastThenFuture(window);
    std::vector<MotionField> out;
    out.reserve(neighbors.size());
    for (size_t i = 0; i < neighbors.size(); ++i) {
      // Zero motion with quality 0 = explicit "no prior" state, src Estimated
      // (never labeled as codec or oracle coverage).
      out.push_back(stages::makeZeroField(kGridStep, center.planes.width,
                                          center.planes.height,
                                          MotionField::Src::Estimated, 0.0f));
    }
    return out;
  }
};

class CodecCoarse final : public CoarseCorrespondence {
 public:
  std::string name() const override { return "codec"; }
  std::vector<MotionField> run(const FrameWindow& window,
                               const NormalizedSideInfo& /*side*/) override {
    const Observation& center = window.center();
    const int w = center.planes.width;
    const int h = center.planes.height;
    const int gw = stages::gridCells(w, kGridStep);
    const int gh = stages::gridCells(h, kGridStep);
    std::vector<MotionField> out;
    for (const Observation* nb : stages::neighborsPastThenFuture(window)) {
      // Past neighbors (windowOffset < 0) take direction<0 vectors
      // (current->previous); future neighbors take direction>0 vectors when
      // any exist. direction==0 vectors are never reused (nothing fabricated).
      // MEASURED on libavcodec 63: export_mvs yields past-reference vectors,
      // so future neighbors typically end up zero/quality 0 here.
      const int wantDirection = nb->windowOffset < 0 ? -1 : 1;
      MotionField f;
      f.src = MotionField::Src::Codec;
      f.gridStep = kGridStep;
      f.gridWidth = gw;
      f.gridHeight = gh;
      f.blocks.reserve(static_cast<size_t>(gw) * static_cast<size_t>(gh));
      for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
          const int ox = gx * kGridStep;
          const int oy = gy * kGridStep;
          BlockVec bv;
          bv.gx = gx;
          bv.gy = gy;
          bv.mv = Vec2f{};
          bv.quality = 0.0f;  // no vector in this cell = no prior
          for (const AnvilMv& mv : center.side.mvs) {
            if (mv.direction != wantDirection) continue;
            if (mv.dstX <= ox && ox < mv.dstX + static_cast<int>(mv.w) &&
                mv.dstY <= oy && oy < mv.dstY + static_cast<int>(mv.h)) {
              // First covering block in fixed vector order wins (blocks are
              // non-overlapping in practice; the rule is deterministic).
              bv.mv = Vec2f{mv.mvX, mv.mvY};
              bv.quality = 1.0f;
              break;
            }
          }
          f.blocks.push_back(bv);
        }
      }
      out.push_back(std::move(f));
    }
    return out;
  }
};

class BlockSearchCoarse final : public CoarseCorrespondence {
 public:
  std::string name() const override { return "blocksearch"; }
  std::vector<MotionField> run(const FrameWindow& window,
                               const NormalizedSideInfo& /*side*/) override {
    const Observation& center = window.center();
    const int w = center.planes.width;
    const int h = center.planes.height;
    const int gw = stages::gridCells(w, kGridStep);
    const int gh = stages::gridCells(h, kGridStep);
    const std::vector<uint8_t>& centerLuma = center.planes.y;
    std::vector<MotionField> out;
    for (const Observation* nb : stages::neighborsPastThenFuture(window)) {
      // Search is symmetric in the neighbor's side: no causal assumption.
      const std::vector<uint8_t>& neighborLuma = nb->planes.y;
      MotionField f;
      f.src = MotionField::Src::Estimated;
      f.gridStep = kGridStep;
      f.gridWidth = gw;
      f.gridHeight = gh;
      f.blocks.reserve(static_cast<size_t>(gw) * static_cast<size_t>(gh));
      for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
          const int bx = gx * kGridStep;
          const int by = gy * kGridStep;
          const int bw = std::min(kGridStep, w - bx);
          const int bh = std::min(kGridStep, h - by);
          // Full search, dy -4..4 outer, dx -4..4 inner (fixed scan order).
          // Keep strictly-better; ties break by smaller |dx|+|dy|, then
          // smaller dy, then smaller dx (explicit rule, independent of scan).
          std::uint64_t bestSsd = 0;
          int bestDx = 0, bestDy = 0;
          bool haveBest = false;
          for (int dy = -kSearchRadius; dy <= kSearchRadius; ++dy) {
            for (int dx = -kSearchRadius; dx <= kSearchRadius; ++dx) {
              const std::uint64_t ssd = stages::lumaBlockSsd(
                  centerLuma, neighborLuma, w, h, bx, by, bw, bh, dx, dy);
              const int candOrder = std::abs(dx) + std::abs(dy);
              const int bestOrder = std::abs(bestDx) + std::abs(bestDy);
              const bool better = !haveBest || ssd < bestSsd ||
                  (ssd == bestSsd &&
                   (candOrder < bestOrder ||
                    (candOrder == bestOrder &&
                     (dy < bestDy || (dy == bestDy && dx < bestDx)))));
              if (better) {
                bestSsd = ssd;
                bestDx = dx;
                bestDy = dy;
                haveBest = true;
              }
            }
          }
          BlockVec bv;
          bv.gx = gx;
          bv.gy = gy;
          bv.mv = Vec2f{static_cast<float>(bestDx), static_cast<float>(bestDy)};
          bv.quality = stages::blockQuality(bestSsd, bw * bh);
          f.blocks.push_back(bv);
        }
      }
      out.push_back(std::move(f));
    }
    return out;
  }
};

}  // namespace

std::unique_ptr<CoarseCorrespondence> makeZeroCoarse() {
  return std::make_unique<ZeroCoarse>();
}

std::unique_ptr<CoarseCorrespondence> makeCodecCoarse() {
  return std::make_unique<CodecCoarse>();
}

std::unique_ptr<CoarseCorrespondence> makeBlockSearchCoarse() {
  return std::make_unique<BlockSearchCoarse>();
}

}  // namespace anvil
