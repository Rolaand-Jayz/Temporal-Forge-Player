// ANVIL stages module: shared deterministic helper implementations.
#include "anvil/stages/StageUtil.hpp"

#include <algorithm>
#include <cmath>

namespace anvil {
namespace stages {

std::vector<const Observation*> neighborsPastThenFuture(const FrameWindow& window) {
  std::vector<const Observation*> out;
  out.reserve(window.observations.size());
  for (size_t i = 0; i < window.observations.size(); ++i) {
    if (i == window.centerIdx) continue;
    out.push_back(window.observations[i].get());
  }
  // Window.hpp contract: observations are ordered past..future, so skipping
  // the center preserves past-then-future neighbor order.
  return out;
}

MotionField makeZeroField(int gridStep, int width, int height,
                          MotionField::Src src, float quality) {
  MotionField f;
  f.src = src;
  f.gridStep = gridStep;
  f.gridWidth = gridCells(width, gridStep);
  f.gridHeight = gridCells(height, gridStep);
  f.blocks.reserve(static_cast<size_t>(f.gridWidth) * static_cast<size_t>(f.gridHeight));
  for (int gy = 0; gy < f.gridHeight; ++gy) {
    for (int gx = 0; gx < f.gridWidth; ++gx) {
      BlockVec bv;
      bv.gx = gx;
      bv.gy = gy;
      bv.mv = Vec2f{};  // zero
      bv.quality = quality;
      f.blocks.push_back(bv);
    }
  }
  return f;
}

float bilinearSampleF(const std::vector<float>& plane, int width, int height,
                      float x, float y) {
  if (width <= 0 || height <= 0 || plane.empty()) return 0.0f;
  // Edge clamp (no wrap, no mirror): fixed policy, deterministic.
  const float cx = std::clamp(x, 0.0f, static_cast<float>(width - 1));
  const float cy = std::clamp(y, 0.0f, static_cast<float>(height - 1));
  const int x0 = static_cast<int>(cx);
  const int y0 = static_cast<int>(cy);
  const int x1 = std::min(x0 + 1, width - 1);
  const int y1 = std::min(y0 + 1, height - 1);
  const float fx = cx - static_cast<float>(x0);
  const float fy = cy - static_cast<float>(y0);
  const float v00 = plane[static_cast<size_t>(y0) * width + x0];
  const float v10 = plane[static_cast<size_t>(y0) * width + x1];
  const float v01 = plane[static_cast<size_t>(y1) * width + x0];
  const float v11 = plane[static_cast<size_t>(y1) * width + x1];
  const float top = v00 + (v10 - v00) * fx;
  const float bottom = v01 + (v11 - v01) * fx;
  return top + (bottom - top) * fy;
}

float bilinearSampleU8(const std::vector<uint8_t>& plane, int width, int height,
                       float x, float y) {
  if (width <= 0 || height <= 0 || plane.empty()) return 0.0f;
  const float cx = std::clamp(x, 0.0f, static_cast<float>(width - 1));
  const float cy = std::clamp(y, 0.0f, static_cast<float>(height - 1));
  const int x0 = static_cast<int>(cx);
  const int y0 = static_cast<int>(cy);
  const int x1 = std::min(x0 + 1, width - 1);
  const int y1 = std::min(y0 + 1, height - 1);
  const float fx = cx - static_cast<float>(x0);
  const float fy = cy - static_cast<float>(y0);
  const float v00 = static_cast<float>(plane[static_cast<size_t>(y0) * width + x0]);
  const float v10 = static_cast<float>(plane[static_cast<size_t>(y0) * width + x1]);
  const float v01 = static_cast<float>(plane[static_cast<size_t>(y1) * width + x0]);
  const float v11 = static_cast<float>(plane[static_cast<size_t>(y1) * width + x1]);
  const float top = v00 + (v10 - v00) * fx;
  const float bottom = v01 + (v11 - v01) * fx;
  return top + (bottom - top) * fy;
}

std::uint64_t lumaBlockSsd(const std::vector<uint8_t>& centerLuma,
                           const std::vector<uint8_t>& neighborLuma,
                           int width, int height,
                           int bx, int by, int bw, int bh,
                           int dx, int dy) {
  std::uint64_t ssd = 0;
  for (int iy = 0; iy < bh; ++iy) {
    const int cy = by + iy;
    const int ny = std::clamp(cy + dy, 0, height - 1);
    for (int ix = 0; ix < bw; ++ix) {
      const int cxp = bx + ix;
      const int nx = std::clamp(cxp + dx, 0, width - 1);
      const int d = static_cast<int>(centerLuma[static_cast<size_t>(cy) * width + cxp]) -
                    static_cast<int>(neighborLuma[static_cast<size_t>(ny) * width + nx]);
      ssd += static_cast<std::uint64_t>(d * d);
    }
  }
  return ssd;
}

float blockQuality(std::uint64_t ssd, int pixels) {
  if (pixels <= 0) return 0.0f;
  const double denom = static_cast<double>(pixels) * 255.0 * 255.0;
  const double q = 1.0 - std::sqrt(static_cast<double>(ssd) / denom);
  return static_cast<float>(std::clamp(q, 0.0, 1.0));
}

}  // namespace stages
}  // namespace anvil
