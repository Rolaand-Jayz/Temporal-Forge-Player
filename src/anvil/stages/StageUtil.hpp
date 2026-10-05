// ANVIL stages module: shared deterministic helpers. Internal to the stages
// module (not part of the pinned stage interfaces in Stages.hpp). Everything
// here is CPU, single-thread, fixed evaluation order — no RNG, no atomics.
#pragma once

#include "anvil/core/Types.hpp"
#include "anvil/core/Window.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace anvil {
namespace stages {

// Grid cells across a dimension: ceil so partial edge blocks cover the full
// image (gridStep 8 over center working dims, 1:1 v1).
inline int gridCells(int dim, int step) {
  return step > 0 ? (dim + step - 1) / step : 0;
}

// Neighbor observations in window order (past then future), center excluded.
// Per-neighbor stage output arrays use this same index order.
std::vector<const Observation*> neighborsPastThenFuture(const FrameWindow& window);

// Zero-motion field over the working grid. One entry per grid cell, index
// (gy * gridWidth + gx), cell origin (gx*step, gy*step).
MotionField makeZeroField(int gridStep, int width, int height,
                          MotionField::Src src, float quality);

// Edge-clamped bilinear sample of a float plane at pixel coordinates (x, y).
float bilinearSampleF(const std::vector<float>& plane, int width, int height,
                      float x, float y);

// Edge-clamped bilinear sample of an 8-bit plane; returns [0,255] float.
float bilinearSampleU8(const std::vector<uint8_t>& plane, int width, int height,
                       float x, float y);

// Sum of squared differences between the center luma block at (bx,by) with
// size (bw,bh) and the neighbor luma at the same region displaced by (dx,dy).
// The center block region is in-bounds by construction; neighbor fetches are
// edge-clamped so the search never reads out of bounds.
std::uint64_t lumaBlockSsd(const std::vector<uint8_t>& centerLuma,
                           const std::vector<uint8_t>& neighborLuma,
                           int width, int height,
                           int bx, int by, int bw, int bh,
                           int dx, int dy);

// Estimator-declared block quality from an SSD over `pixels` luma samples:
// 1 - sqrt(ssd / (pixels * 255^2)), clamped to [0,1]. For a full 8x8 block
// this is exactly the 64*255^2 normalization.
float blockQuality(std::uint64_t ssd, int pixels);

}  // namespace stages
}  // namespace anvil
