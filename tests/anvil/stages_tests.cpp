// ANVIL stages module tests (owned by the stages implementation worker).
//
// Coverage (ask item 8): factories produce every named implementation and
// return nullptr for unknown names (documented convention; oracle twins are
// wired by the runner, so "oracle" is nullptr here too); zero coarse is the
// control; blocksearch recovers an exact integer translation on a synthetic
// textured image; hillclimb2 improves SSD and lands within 0.25px of a known
// half-pixel shift; photo_fwbw gives weight ~0 in a corrupted region and ~1
// elsewhere; weighted_blend with exact translation + weight 1 reproduces the
// mean of center+neighbor; center reconstruct is bytes-equal to the center;
// global_xform recovers a known pure translation to within 1e-3; geometry
// default is Unknown.
#include "anvil/stages/StageFactories.hpp"
#include "anvil/stages/StageUtil.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
  do {                                                                       \
    const double va_ = static_cast<double>(a);                               \
    const double vb_ = static_cast<double>(b);                               \
    if (!(std::fabs(va_ - vb_) <= (eps))) {                                  \
      ++g_failures;                                                          \
      std::printf("FAIL %s:%d: |%s(%g) - %s(%g)| > %g\n", __FILE__,          \
                  __LINE__, #a, va_, #b, vb_, static_cast<double>(eps));     \
    }                                                                        \
  } while (0)

using anvil::AnvilMv;
using anvil::BlockVec;
using anvil::CoarseCorrespondence;
using anvil::FrameWindow;
using anvil::MotionField;
using anvil::NormalizedSideInfo;
using anvil::Observation;
using anvil::ReconstructionResult;
using anvil::SampleGeometry;
using anvil::SideInfo;
using anvil::StageBundle;
using anvil::Vec2f;
using anvil::Visibility;
using anvil::WorkingImage;

// Deterministic texture: aperiodic enough that a +-4px full search has a
// unique optimum (no RNG anywhere — fixed integer formula).
uint8_t tex(int x, int y) {
  const int v = (x * 37 + y * 61 + x * y * 7) % 251;
  return static_cast<uint8_t>(v);
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::shared_ptr<Observation> makeObs(uint64_t fileFrameIndex, int w, int h,
                                     int windowOffset,
                                     const std::function<uint8_t(int, int)>& fillY) {
  auto o = std::make_shared<Observation>();
  o->id.fileFrameIndex = fileFrameIndex;
  o->id.ptsUs = static_cast<int64_t>(fileFrameIndex) * 33367;
  o->windowOffset = windowOffset;
  o->rel = windowOffset < 0   ? Observation::Rel::Past
           : windowOffset > 0 ? Observation::Rel::Future
                              : Observation::Rel::Center;
  o->planes.width = w;
  o->planes.height = h;
  o->planes.y.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
  o->planes.u.assign(static_cast<size_t>(w / 2) * static_cast<size_t>(h / 2), 128);
  o->planes.v.assign(static_cast<size_t>(w / 2) * static_cast<size_t>(h / 2), 128);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      o->planes.y[static_cast<size_t>(y) * w + x] = fillY(x, y);
    }
  }
  return o;
}

FrameWindow makeWindow(const std::vector<std::shared_ptr<Observation>>& obs,
                       size_t centerIdx) {
  FrameWindow w;
  w.observations.assign(obs.begin(), obs.end());  // shared_ptr<Obs> -> shared_ptr<const Obs>
  w.centerIdx = centerIdx;
  return w;
}

WorkingImage makeImage(int w, int h,
                       const std::function<float(int, int)>& fill) {
  WorkingImage img;
  img.width = w;
  img.height = h;
  img.r.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
  img.g.resize(img.r.size());
  img.b.resize(img.r.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float v = fill(x, y);
      const size_t p = static_cast<size_t>(y) * w + x;
      img.r[p] = v;
      img.g[p] = v;
      img.b[p] = v;
    }
  }
  return img;
}

// Zero-motion field with all blocks declared quality 1 (test input).
MotionField unitQualityZeroField(int w, int h) {
  return anvil::stages::makeZeroField(8, w, h, MotionField::Src::Estimated, 1.0f);
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

void testFactoriesProduceAllNamed() {
  auto norm = anvil::createSideInfoNormalizer("passthrough");
  CHECK(norm && norm->name() == "passthrough");
  norm = anvil::createSideInfoNormalizer("densify_blocks");
  CHECK(norm && norm->name() == "densify_blocks");

  auto coarse = anvil::createCoarse("zero");
  CHECK(coarse && coarse->name() == "zero");
  coarse = anvil::createCoarse("codec");
  CHECK(coarse && coarse->name() == "codec");
  coarse = anvil::createCoarse("blocksearch");
  CHECK(coarse && coarse->name() == "blocksearch");

  auto ref = anvil::createRefiner("none");
  CHECK(ref && ref->name() == "none");
  ref = anvil::createRefiner("hillclimb2");
  CHECK(ref && ref->name() == "hillclimb2");

  auto vis = anvil::createVisibility("unity");
  CHECK(vis && vis->name() == "unity");
  vis = anvil::createVisibility("photo_fwbw");
  CHECK(vis && vis->name() == "photo_fwbw");

  auto geo = anvil::createGeometry("unknown");
  CHECK(geo && geo->name() == "unknown");
  geo = anvil::createGeometry("global_xform");
  CHECK(geo && geo->name() == "global_xform");

  auto rec = anvil::createReconstructor("center");
  CHECK(rec && rec->name() == "center");
  rec = anvil::createReconstructor("weighted_blend");
  CHECK(rec && rec->name() == "weighted_blend");

  auto be = anvil::createBackend("null");
  CHECK(be && be->name() == "null");
  be = anvil::createBackend("passthrough");
  CHECK(be && be->name() == "passthrough");
}

void testFactoriesRejectUnknownNames() {
  // Convention: unknown names return nullptr. Oracle twins ("oracle") are
  // wired by the runner from oracle/, not by this factory set.
  const char* kUnknown[] = {"", "nope", "oracle", "PASSTHROUGH", "zero "};
  for (const char* n : kUnknown) {
    const std::string name(n);
    CHECK(anvil::createSideInfoNormalizer(name) == nullptr);
    CHECK(anvil::createCoarse(name) == nullptr);
    CHECK(anvil::createRefiner(name) == nullptr);
    CHECK(anvil::createVisibility(name) == nullptr);
    CHECK(anvil::createGeometry(name) == nullptr);
    CHECK(anvil::createReconstructor(name) == nullptr);
    CHECK(anvil::createBackend(name) == nullptr);
  }
}

// ---------------------------------------------------------------------------
// SideInfoNormalizer
// ---------------------------------------------------------------------------

void testNormalizerPassthrough() {
  NormalizedSideInfo in;
  in.side.provenance = SideInfo::Provenance::External;
  in.side.coverage = SideInfo::Coverage::Present;
  auto n = anvil::createSideInfoNormalizer("passthrough");
  auto obs = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  const FrameWindow win = makeWindow({obs}, 0);
  const NormalizedSideInfo out = n->run(win, in);
  CHECK(out.side.provenance == SideInfo::Provenance::External);
  CHECK(out.densifiedPrior == nullptr);
}

void testNormalizerDensifyBlocks() {
  auto n = anvil::createSideInfoNormalizer("densify_blocks");
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  center->side.provenance = SideInfo::Provenance::CodecMv;
  center->side.coverage = SideInfo::Coverage::Present;
  AnvilMv mv;
  mv.dstX = 8;
  mv.dstY = 8;
  mv.w = 8;
  mv.h = 8;
  mv.mvX = 1.5f;
  mv.mvY = -2.0f;
  mv.direction = -1;
  center->side.mvs.push_back(mv);
  const FrameWindow win = makeWindow({center}, 0);

  NormalizedSideInfo in;
  const NormalizedSideInfo out = n->run(win, in);
  // Densified prior exists, labeled src=Codec (never relabeled as estimated).
  CHECK(out.densifiedPrior != nullptr);
  if (out.densifiedPrior) {
    const MotionField& f = *out.densifiedPrior;
    CHECK(f.src == MotionField::Src::Codec);
    CHECK(f.gridStep == 8);
    CHECK(f.gridWidth == 4 && f.gridHeight == 4);
    CHECK(f.blocks.size() == 16);
    // The block at dst (8,8) covers grid cell (1,1); other cells are
    // zero/quality 0 (no fabricated coverage).
    const BlockVec& covered = f.blocks[static_cast<size_t>(1) * 4 + 1];
    CHECK_NEAR(covered.mv.x, 1.5, 1e-6);
    CHECK_NEAR(covered.mv.y, -2.0, 1e-6);
    for (size_t i = 0; i < f.blocks.size(); ++i) {
      if (i == 5) continue;
      CHECK(f.blocks[i].quality == 0.0f);
    }
  }
  // Raw side info preserved alongside the densified form.
  CHECK(out.side.provenance == SideInfo::Provenance::CodecMv);
  CHECK(out.side.coverage == SideInfo::Coverage::Present);
  CHECK(out.side.mvs.size() == 1);
}

// ---------------------------------------------------------------------------
// CoarseCorrespondence
// ---------------------------------------------------------------------------

void testZeroCoarseControl() {
  auto coarse = anvil::createCoarse("zero");
  auto past = makeObs(9, 32, 32, -1, [](int, int) { return uint8_t{64}; });
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  auto future = makeObs(11, 32, 32, 1, [](int, int) { return uint8_t{192}; });
  const FrameWindow win = makeWindow({past, center, future}, 1);
  NormalizedSideInfo side;
  const std::vector<MotionField> out = coarse->run(win, side);
  CHECK(out.size() == 2);  // past then future, index-aligned
  for (const MotionField& f : out) {
    CHECK(f.src == MotionField::Src::Estimated);
    CHECK(f.gridStep == 8);
    CHECK(f.gridWidth == 4 && f.gridHeight == 4);
    CHECK(f.blocks.size() == 16);
    for (const BlockVec& bv : f.blocks) {
      CHECK(bv.mv.x == 0.0f && bv.mv.y == 0.0f);
      CHECK(bv.quality == 0.0f);
    }
  }
}

void testCodecCoarseDirectionRasterization() {
  auto coarse = anvil::createCoarse("codec");
  auto past = makeObs(9, 32, 32, -1, [](int, int) { return uint8_t{64}; });
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  auto future = makeObs(11, 32, 32, 1, [](int, int) { return uint8_t{192}; });
  center->side.provenance = SideInfo::Provenance::CodecMv;
  AnvilMv pastMv;   // current->previous (direction<0)
  pastMv.dstX = 8;
  pastMv.dstY = 8;
  pastMv.w = 8;
  pastMv.h = 8;
  pastMv.mvX = 1.5f;
  pastMv.mvY = -2.0f;
  pastMv.direction = -1;
  AnvilMv futureMv;  // current->next (direction>0), e.g. bi-pred second list
  futureMv.dstX = 16;
  futureMv.dstY = 16;
  futureMv.w = 8;
  futureMv.h = 8;
  futureMv.mvX = 0.5f;
  futureMv.mvY = 0.5f;
  futureMv.direction = 1;
  center->side.mvs = {pastMv, futureMv};
  const FrameWindow win = makeWindow({past, center, future}, 1);
  NormalizedSideInfo side;
  const std::vector<MotionField> out = coarse->run(win, side);
  CHECK(out.size() == 2);
  CHECK(out[0].src == MotionField::Src::Codec);
  // Past neighbor: only direction<0 vectors apply.
  const BlockVec& p11 = out[0].blocks[static_cast<size_t>(1) * 4 + 1];
  CHECK_NEAR(p11.mv.x, 1.5, 1e-6);
  CHECK_NEAR(p11.mv.y, -2.0, 1e-6);
  CHECK(p11.quality == 1.0f);
  const BlockVec& p22 = out[0].blocks[static_cast<size_t>(2) * 4 + 2];
  CHECK(p22.mv.x == 0.0f && p22.mv.y == 0.0f);
  CHECK(p22.quality == 0.0f);
  // Future neighbor: only direction>0 vectors apply.
  const BlockVec& f22 = out[1].blocks[static_cast<size_t>(2) * 4 + 2];
  CHECK_NEAR(f22.mv.x, 0.5, 1e-6);
  CHECK_NEAR(f22.mv.y, 0.5, 1e-6);
  CHECK(f22.quality == 1.0f);
  const BlockVec& f11 = out[1].blocks[static_cast<size_t>(1) * 4 + 1];
  CHECK(f11.mv.x == 0.0f && f11.mv.y == 0.0f);
  CHECK(f11.quality == 0.0f);
}

void testBlockSearchRecoversExactTranslation() {
  auto coarse = anvil::createCoarse("blocksearch");
  const int w = 64, h = 64;
  // Past neighbor = center content translated by (3,-2): N(x,y) = C(x-3,y+2).
  auto center = makeObs(10, w, h, 0, [](int x, int y) { return tex(x, y); });
  auto past = makeObs(9, w, h, -1, [](int x, int y) {
    return tex(clampi(x - 3, 0, 63), clampi(y + 2, 0, 63));
  });
  // Future neighbor = center translated by (-1,2): search is symmetric.
  auto future = makeObs(11, w, h, 1, [](int x, int y) {
    return tex(clampi(x + 1, 0, 63), clampi(y - 2, 0, 63));
  });
  const FrameWindow win = makeWindow({past, center, future}, 1);
  NormalizedSideInfo side;
  const std::vector<MotionField> out = coarse->run(win, side);
  CHECK(out.size() == 2);
  CHECK(out[0].src == MotionField::Src::Estimated);
  CHECK(out[0].gridWidth == 8 && out[0].gridHeight == 8);
  const auto expectMv = [](const MotionField& f, int gx, int gy, float ex,
                           float ey) {
    const BlockVec& bv = f.blocks[static_cast<size_t>(gy) * 8 + gx];
    CHECK_NEAR(bv.mv.x, ex, 1e-6);
    CHECK_NEAR(bv.mv.y, ey, 1e-6);
    CHECK(bv.quality > 0.99f);
  };
  // Interior blocks recover the exact integer translation at quality ~1.
  expectMv(out[0], 2, 2, 3.0f, -2.0f);
  expectMv(out[0], 4, 3, 3.0f, -2.0f);
  expectMv(out[1], 3, 4, -1.0f, 2.0f);
  expectMv(out[1], 5, 2, -1.0f, 2.0f);
}

// ---------------------------------------------------------------------------
// ResidualRefiner
// ---------------------------------------------------------------------------

void testHillClimbLandsOnHalfPixelShift() {
  // Smooth horizontal ramp C(x)=4x; neighbor is the half-pixel-shifted ramp
  // N(x)=4x-2, so N(x+0.5)=C(x) and the true mv is +0.5px. SSD is exactly
  // quadratic in the integer offset, so the parabola fit must land at 0.5.
  auto refiner = anvil::createRefiner("hillclimb2");
  const int w = 64, h = 16;
  auto center = makeObs(10, w, h, 0,
                        [](int x, int) { return static_cast<uint8_t>(4 * x); });
  auto past = makeObs(9, w, h, -1, [](int x, int) {
    return static_cast<uint8_t>(clampi(4 * x - 2, 0, 255));
  });
  const FrameWindow win = makeWindow({past, center}, 1);
  auto zero = anvil::createCoarse("zero");
  const std::vector<MotionField> seeds = zero->run(win, NormalizedSideInfo{});
  const std::vector<MotionField> out = refiner->run(win, seeds);
  CHECK(out.size() == 1);
  const int gx[] = {2, 3, 5, 6};
  for (int b : gx) {
    const BlockVec& bv = out[0].blocks[static_cast<size_t>(1) * 8 + static_cast<size_t>(b)];
    CHECK_NEAR(bv.mv.x, 0.5, 0.25);
    CHECK_NEAR(bv.mv.y, 0.0, 0.25);
  }
}

void testHillClimbImprovesSsd() {
  // Smooth plane C(x,y)=4x+y; neighbor translated by (2,1). From the zero
  // seed, hill climbing strictly decreases SSD; for block (2,2) the SSD at
  // the refined integer position must be below the seed SSD.
  auto refiner = anvil::createRefiner("hillclimb2");
  const int w = 32, h = 32;
  auto center = makeObs(10, w, h, 0, [](int x, int y) {
    return static_cast<uint8_t>(clampi(4 * x + y, 0, 255));
  });
  auto past = makeObs(9, w, h, -1, [](int x, int y) {
    return static_cast<uint8_t>(clampi(4 * (x - 2) + (y - 1), 0, 255));
  });
  const FrameWindow win = makeWindow({past, center}, 1);
  auto zero = anvil::createCoarse("zero");
  const std::vector<MotionField> seeds = zero->run(win, NormalizedSideInfo{});
  const std::vector<MotionField> out = refiner->run(win, seeds);
  CHECK(out.size() == 1);
  const BlockVec& bv = out[0].blocks[static_cast<size_t>(2) * 4 + 2];
  const std::uint64_t seedSsd = anvil::stages::lumaBlockSsd(
      center->planes.y, past->planes.y, w, h, 16, 16, 8, 8, 0, 0);
  const std::uint64_t refinedSsd = anvil::stages::lumaBlockSsd(
      center->planes.y, past->planes.y, w, h, 16, 16, 8, 8,
      static_cast<int>(std::lround(bv.mv.x)), static_cast<int>(std::lround(bv.mv.y)));
  CHECK(refinedSsd < seedSsd);
}

void testRefinerNoneReturnsSeeds() {
  auto refiner = anvil::createRefiner("none");
  auto past = makeObs(9, 16, 16, -1, [](int, int) { return uint8_t{7}; });
  auto center = makeObs(10, 16, 16, 0, [](int, int) { return uint8_t{9}; });
  const FrameWindow win = makeWindow({past, center}, 1);
  std::vector<MotionField> seeds;
  MotionField f = unitQualityZeroField(16, 16);
  f.blocks[3].mv = Vec2f{1.0f, 2.0f};
  seeds.push_back(f);
  const std::vector<MotionField> out = refiner->run(win, seeds);
  CHECK(out.size() == 1);
  CHECK(out[0].blocks.size() == f.blocks.size());
  CHECK(out[0].blocks[3].mv.x == 1.0f && out[0].blocks[3].mv.y == 2.0f);
}

// ---------------------------------------------------------------------------
// VisibilityEstimator
// ---------------------------------------------------------------------------

void testUnityVisibility() {
  auto vis = anvil::createVisibility("unity");
  auto past = makeObs(9, 24, 24, -1, [](int, int) { return uint8_t{1}; });
  auto center = makeObs(10, 24, 24, 0, [](int, int) { return uint8_t{2}; });
  const FrameWindow win = makeWindow({past, center}, 1);
  const std::vector<Visibility> out =
      vis->run(win, {unitQualityZeroField(24, 24)});
  CHECK(out.size() == 1);
  CHECK(out[0].src == anvil::Visibility::Src::Estimated);
  CHECK(out[0].width == 24 && out[0].height == 24);
  CHECK(out[0].weight.size() == 24 * 24);
  for (float wt : out[0].weight) CHECK(wt == 1.0f);
  for (uint8_t o : out[0].occluded) CHECK(o == 0);
}

void testPhotoFwBwWeights() {
  auto vis = anvil::createVisibility("photo_fwbw");
  const int w = 32, h = 32;
  // Center flat gray; neighbor identical except a corrupted black rectangle
  // at [8,16)x[8,16). Zero motion -> residual 0 outside, large inside.
  auto center = makeObs(10, w, h, 0, [](int, int) { return uint8_t{128}; });
  auto past = makeObs(9, w, h, -1, [](int x, int y) {
    const bool corrupted = x >= 8 && x < 16 && y >= 8 && y < 16;
    return static_cast<uint8_t>(corrupted ? 0 : 128);
  });
  const FrameWindow win = makeWindow({past, center}, 1);
  const std::vector<Visibility> out =
      vis->run(win, {unitQualityZeroField(w, h)});
  CHECK(out.size() == 1);
  const Visibility& v = out[0];
  CHECK(v.weight.size() == static_cast<size_t>(w) * h);
  CHECK_NEAR(v.weight[static_cast<size_t>(4) * w + 4], 1.0, 1e-2);
  CHECK_NEAR(v.weight[static_cast<size_t>(28) * w + 28], 1.0, 1e-2);
  // Corrupted region: residual ~0.5 normalized -> exp(-(0.5/0.08)^2) ~ 0.
  CHECK(v.weight[static_cast<size_t>(10) * w + 10] < 0.01f);
  CHECK(v.weight[static_cast<size_t>(14) * w + 12] < 0.01f);
  // fwbwError slot not populated: no occlusion flags.
  for (uint8_t o : v.occluded) CHECK(o == 0);
}

void testPhotoFwBwFwbwOcclusion() {
  auto vis = anvil::createVisibility("photo_fwbw");
  const int w = 32, h = 32;
  auto center = makeObs(10, w, h, 0, [](int, int) { return uint8_t{128}; });
  auto past = makeObs(9, w, h, -1, [](int, int) { return uint8_t{128}; });
  const FrameWindow win = makeWindow({past, center}, 1);
  MotionField mf = unitQualityZeroField(w, h);
  mf.fwbwError.assign(16, 0.0f);
  mf.fwbwError[1] = 0.5f;  // grid cell (gx=1, gy=0) exceeds the 0.35 threshold
  const std::vector<Visibility> out = vis->run(win, {mf});
  CHECK(out.size() == 1);
  // Every pixel of block (1,0) (x in [8,16), y in [0,8)) is occluded.
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 16; ++x) {
      const size_t p = static_cast<size_t>(y) * w + static_cast<size_t>(x);
      const bool expectOcc = x >= 8;
      CHECK(out[0].occluded[p] == (expectOcc ? 1 : 0));
      if (expectOcc) CHECK(out[0].weight[p] == 0.0f);
    }
  }
}

// ---------------------------------------------------------------------------
// SampleGeometryEstimator
// ---------------------------------------------------------------------------

void testGeometryDefaultUnknown() {
  auto geo = anvil::createGeometry("unknown");
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  const FrameWindow win = makeWindow({center}, 0);
  const SampleGeometry g = geo->run(win, {unitQualityZeroField(32, 32)});
  CHECK(g.status == SampleGeometry::Status::Unknown);
}

void testGlobalXformRecoversTranslation() {
  auto geo = anvil::createGeometry("global_xform");
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  const FrameWindow win = makeWindow({center}, 0);
  MotionField mf = unitQualityZeroField(32, 32);
  for (BlockVec& bv : mf.blocks) bv.mv = Vec2f{1.5f, -0.75f};
  const SampleGeometry g = geo->run(win, {mf});
  CHECK(g.status == SampleGeometry::Status::Estimated);
  CHECK_NEAR(g.globalXform.a, 1.0, 1e-3);
  CHECK_NEAR(g.globalXform.b, 0.0, 1e-3);
  CHECK_NEAR(g.globalXform.c, 1.5, 1e-3);
  CHECK_NEAR(g.globalXform.d, 0.0, 1e-3);
  CHECK_NEAR(g.globalXform.e, 1.0, 1e-3);
  CHECK_NEAR(g.globalXform.f, -0.75, 1e-3);
}

void testGlobalXformDegenerateUnknown() {
  auto geo = anvil::createGeometry("global_xform");
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{128}; });
  const FrameWindow win = makeWindow({center}, 0);
  // All blocks below the 0.5 quality gate: 0 correspondences -> Unknown.
  MotionField lowQ = anvil::stages::makeZeroField(8, 32, 32,
                                                  MotionField::Src::Estimated, 0.3f);
  CHECK(geo->run(win, {lowQ}).status == SampleGeometry::Status::Unknown);
  // 16x16 image -> 4 blocks < 8 correspondences -> Unknown.
  MotionField few = unitQualityZeroField(16, 16);
  CHECK(geo->run(win, {few}).status == SampleGeometry::Status::Unknown);
}

// ---------------------------------------------------------------------------
// TemporalReconstructor + OutputBackend
// ---------------------------------------------------------------------------

void testCenterReconstructBytesEqual() {
  auto rec = anvil::createReconstructor("center");
  auto past = makeObs(9, 16, 16, -1, [](int, int) { return uint8_t{3}; });
  auto center = makeObs(10, 16, 16, 0, [](int, int) { return uint8_t{5}; });
  const FrameWindow win = makeWindow({past, center}, 1);
  StageBundle bundle;
  bundle.center = makeImage(16, 16, [](int x, int y) {
    return static_cast<float>((x * 3 + y) % 7) / 7.0f;
  });
  bundle.neighbors.push_back(makeImage(16, 16, [](int, int) { return 0.5f; }));
  bundle.neighborFrameIndices = {9};
  bundle.neighborMotion.push_back(unitQualityZeroField(16, 16));
  anvil::Visibility v;
  v.width = 16;
  v.height = 16;
  v.weight.assign(16 * 16, 1.0f);
  v.occluded.assign(16 * 16, 0);
  bundle.neighborVisibility.push_back(v);
  bundle.geometry = SampleGeometry{};

  const ReconstructionResult r = rec->run(win, bundle);
  CHECK(r.fileFrameIndex == 10);
  CHECK(r.ptsUs == center->id.ptsUs);
  CHECK(r.out.width == 16 && r.out.height == 16);
  CHECK(r.out.r == bundle.center.r);
  CHECK(r.out.g == bundle.center.g);
  CHECK(r.out.b == bundle.center.b);
  // Ablation lists the center only.
  CHECK(r.ablation.usedFrameIndices.size() == 1);
  CHECK(r.ablation.usedFrameIndices[0] == 10);
  CHECK(r.ablation.droppedFrameIndices.empty());
}

void testWeightedBlendMeanOfCenterAndNeighbor() {
  auto rec = anvil::createReconstructor("weighted_blend");
  auto past = makeObs(9, 32, 32, -1, [](int, int) { return uint8_t{1}; });
  auto center = makeObs(10, 32, 32, 0, [](int, int) { return uint8_t{2}; });
  const FrameWindow win = makeWindow({past, center}, 1);
  const int w = 32, h = 32;
  StageBundle bundle;
  bundle.center = makeImage(w, h, [](int x, int y) {
    return static_cast<float>((x * 3 + y * 5) % 64) / 64.0f;
  });
  // Neighbor content D translated: N(x,y) = D(x-2, y+1) so that sampling N
  // at p + (2,-1) yields exactly D(p) — an exact translation, weight 1.
  // D(x,y) is the neighbor's own pattern; the blend result must be the mean
  // of center(p) and D(p).
  const auto dval = [](int x, int y) {
    const int m = (x * 7 + y * 11) % 64;
    return static_cast<float>((m % 64 + 64) % 64) / 64.0f;
  };
  bundle.neighbors.push_back(makeImage(w, h, [&](int x, int y) {
    return dval(x - 2, y + 1);
  }));
  bundle.neighborFrameIndices = {9};
  MotionField mf = unitQualityZeroField(w, h);
  for (BlockVec& bv : mf.blocks) bv.mv = Vec2f{2.0f, -1.0f};
  bundle.neighborMotion.push_back(mf);
  anvil::Visibility v;
  v.width = w;
  v.height = h;
  v.weight.assign(static_cast<size_t>(w) * h, 1.0f);
  v.occluded.assign(static_cast<size_t>(w) * h, 0);
  bundle.neighborVisibility.push_back(v);
  bundle.geometry = SampleGeometry{};

  const ReconstructionResult r = rec->run(win, bundle);
  CHECK(r.out.width == w && r.out.height == h);
  // Exact-mean holds wherever p + mv is in bounds (bilinear at integer
  // coordinates is exact); the top row and right edge clamp, documented
  // edge policy, so they are excluded from the exact assertion.
  for (int y = 1; y < h; ++y) {
    for (int x = 0; x < w - 2; ++x) {
      const size_t p = static_cast<size_t>(y) * w + static_cast<size_t>(x);
      const float expected = 0.5f * (bundle.center.r[p] + dval(x, y));
      CHECK_NEAR(r.out.r[p], expected, 1e-5);
      CHECK_NEAR(r.out.g[p], expected, 1e-5);
      CHECK_NEAR(r.out.b[p], expected, 1e-5);
    }
  }
  // Neighbor contributed everywhere -> used; nothing dropped.
  CHECK(r.ablation.usedFrameIndices.size() == 1);
  CHECK(r.ablation.usedFrameIndices[0] == 9);
  CHECK(r.ablation.droppedFrameIndices.empty());
}

void testWeightedBlendOccludedNeighborExcluded() {
  auto rec = anvil::createReconstructor("weighted_blend");
  auto past = makeObs(9, 16, 16, -1, [](int, int) { return uint8_t{1}; });
  auto center = makeObs(10, 16, 16, 0, [](int, int) { return uint8_t{2}; });
  const FrameWindow win = makeWindow({past, center}, 1);
  StageBundle bundle;
  bundle.center = makeImage(16, 16, [](int, int) { return 0.25f; });
  bundle.neighbors.push_back(makeImage(16, 16, [](int, int) { return 0.75f; }));
  bundle.neighborFrameIndices = {9};
  bundle.neighborMotion.push_back(unitQualityZeroField(16, 16));
  anvil::Visibility v;
  v.width = 16;
  v.height = 16;
  v.weight.assign(16 * 16, 0.0f);  // zero weight everywhere: excluded
  v.occluded.assign(16 * 16, 0);
  bundle.neighborVisibility.push_back(v);
  bundle.geometry = SampleGeometry{};
  const ReconstructionResult r = rec->run(win, bundle);
  // Only the center contributes: out == center exactly.
  CHECK(r.out.r == bundle.center.r);
  CHECK(r.ablation.usedFrameIndices.empty());
}

void testBackends() {
  auto nullBe = anvil::createBackend("null");
  auto passBe = anvil::createBackend("passthrough");
  ReconstructionResult r;
  r.out = makeImage(8, 4, [](int x, int y) {
    return static_cast<float>(x + y) / 16.0f;
  });
  const WorkingImage zeroed = nullBe->present(r);
  CHECK(zeroed.width == 8 && zeroed.height == 4);
  CHECK(zeroed.r.size() == 32);
  for (float v : zeroed.r) CHECK(v == 0.0f);
  for (float v : zeroed.g) CHECK(v == 0.0f);
  for (float v : zeroed.b) CHECK(v == 0.0f);
  const WorkingImage passed = passBe->present(r);
  CHECK(passed.r == r.out.r);
  CHECK(passed.g == r.out.g);
  CHECK(passed.b == r.out.b);
}

}  // namespace

int main() {
  testFactoriesProduceAllNamed();
  testFactoriesRejectUnknownNames();
  testNormalizerPassthrough();
  testNormalizerDensifyBlocks();
  testZeroCoarseControl();
  testCodecCoarseDirectionRasterization();
  testBlockSearchRecoversExactTranslation();
  testHillClimbLandsOnHalfPixelShift();
  testHillClimbImprovesSsd();
  testRefinerNoneReturnsSeeds();
  testUnityVisibility();
  testPhotoFwBwWeights();
  testPhotoFwBwFwbwOcclusion();
  testGeometryDefaultUnknown();
  testGlobalXformRecoversTranslation();
  testGlobalXformDegenerateUnknown();
  testCenterReconstructBytesEqual();
  testWeightedBlendMeanOfCenterAndNeighbor();
  testWeightedBlendOccludedNeighborExcluded();
  testBackends();
  if (g_failures == 0) {
    std::printf("anvil_stages_tests: all checks passed\n");
    return 0;
  }
  std::printf("anvil_stages_tests: %d check(s) FAILED\n", g_failures);
  return 1;
}
