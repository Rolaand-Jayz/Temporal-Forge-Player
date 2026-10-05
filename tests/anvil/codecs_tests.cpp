// ANVIL codecs module tests (owned by the codecs implementation worker).
// Contract tests for CodecSideInfoClassifier (measured capability table,
// reconciliation 3.9) and BlockDensifier (nearest-block densification,
// discontinuity-preserving). Deterministic: no RNG, fixed orders.
#include "anvil/codecs/CodecSideInfo.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace anvil;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++g_failures;
  }
}

SideInfo sideWithMvs(std::vector<AnvilMv> mvs) {
  SideInfo side;
  side.provenance = SideInfo::Provenance::CodecMv;
  side.mvs = std::move(mvs);
  return side;
}

AnvilMv block(int dstX, int dstY, int w, int h, float mvX, float mvY) {
  AnvilMv b;
  b.dstX = static_cast<int16_t>(dstX);
  b.dstY = static_cast<int16_t>(dstY);
  b.w = static_cast<uint16_t>(w);
  b.h = static_cast<uint16_t>(h);
  b.mvX = mvX;
  b.mvY = mvY;
  b.direction = -1;
  return b;
}

const BlockVec* cellAt(const MotionField& f, int gx, int gy) {
  if (gx < 0 || gy < 0 || gx >= f.gridWidth || gy >= f.gridHeight) return nullptr;
  return &f.blocks[static_cast<size_t>(gy) * static_cast<size_t>(f.gridWidth) +
                   static_cast<size_t>(gx)];
}

// --- Classification table (MEASURED capability cells, reconciliation 3.9) ---

void testClassifierTable() {
  // h264 software: only measured export-capable codec on this stack.
  CodecSideInfoClassifier h264("h264", /*hwDecode=*/false);
  check(h264.capability().codecName == "h264", "h264 capability name stored");
  check(h264.capability().codecMvExport, "h264 measured as mv-export capable");
  check(!h264.capability().hwDecodePath, "h264 sw path not flagged hw");

  SideInfo pFrame = sideWithMvs({block(0, 0, 16, 16, 1.0f, 0.5f)});
  check(h264.classify(pFrame, /*isIFrame=*/false) == SideInfo::Coverage::Present,
        "h264 sw P frame with vectors -> Present");

  SideInfo iFrame = sideWithMvs({});  // I frames emit no side data (measured)
  check(h264.classify(iFrame, /*isIFrame=*/true) == SideInfo::Coverage::IntraFrame,
        "h264 sw I frame empty -> IntraFrame");

  // Non-I frame with empty mvs: truthful Present-with-empty, NEVER IntraFrame.
  SideInfo pEmpty = sideWithMvs({});
  SideInfo::Coverage c = h264.classify(pEmpty, /*isIFrame=*/false);
  check(c == SideInfo::Coverage::Present, "h264 sw P frame EMPTY mvs -> Present (no vectors this frame)");
  check(c != SideInfo::Coverage::IntraFrame, "h264 sw P frame empty must not be labeled IntraFrame");
  check(pEmpty.mvs.empty(), "empty+Present stays empty; consumer decides");

  // hevc: measured no export (libavcodec 63.1.102, dwfrun-015b2ece).
  CodecSideInfoClassifier hevc("hevc", false);
  check(!hevc.capability().codecMvExport, "hevc measured as NOT export capable");
  check(hevc.classify(pFrame, false) == SideInfo::Coverage::UnsupportedCodec,
        "hevc P frame -> UnsupportedCodec");
  check(hevc.classify(iFrame, true) == SideInfo::Coverage::UnsupportedCodec,
        "hevc I frame -> UnsupportedCodec (capability precedes frame type)");

  // av1: measured no export (libdav1d absent).
  CodecSideInfoClassifier av1("av1", false);
  check(!av1.capability().codecMvExport, "av1 measured as NOT export capable");
  check(av1.classify(pFrame, false) == SideInfo::Coverage::UnsupportedCodec,
        "av1 P frame -> UnsupportedCodec");

  // Unknown codec: conservative -> not export capable.
  CodecSideInfoClassifier vp9("vp9", false);
  check(!vp9.capability().codecMvExport, "unknown codec conservatively NOT export capable");
  check(vp9.classify(pFrame, false) == SideInfo::Coverage::UnsupportedCodec,
        "unknown codec -> UnsupportedCodec");

  // Hardware decode: measured no export on every hwaccel path (VAAPI/Vulkan,
  // Navi31/Mesa 26.2.4) — hw flag wins over the codec table.
  CodecSideInfoClassifier h264hw("h264", /*hwDecode=*/true);
  check(h264hw.capability().codecMvExport, "h264 table entry independent of hw flag");
  check(h264hw.capability().hwDecodePath, "hw flag stored");
  check(h264hw.classify(pFrame, false) == SideInfo::Coverage::HwDecodeNoExport,
        "h264 hw P frame with vectors -> HwDecodeNoExport");
  check(h264hw.classify(iFrame, true) == SideInfo::Coverage::HwDecodeNoExport,
        "h264 hw I frame -> HwDecodeNoExport");
}

// --- Densifier: full coverage, 1:1 scale ---

void testDensifyFullCoverage() {
  BlockDensifier dens(8, 16, 16);
  std::vector<AnvilMv> mvs = {block(0, 0, 16, 16, 1.5f, -2.25f)};
  MotionField f = dens.densify(mvs, 16, 16);

  check(f.src == MotionField::Src::Codec, "densified field labeled Src::Codec");
  check(f.gridStep == 8, "gridStep propagated");
  check(f.gridWidth == 2 && f.gridHeight == 2, "16x16 @ step 8 -> 2x2 grid");
  check(f.blocks.size() == 4, "blocks sized gridWidth*gridHeight");
  check(f.fwbwError.empty(), "no fabricated fwbw error");

  for (const BlockVec& b : f.blocks) {
    const std::string tag = "cell(" + std::to_string(b.gx) + "," + std::to_string(b.gy) + ")";
    check(b.mv.x == 1.5f && b.mv.y == -2.25f, tag + " carries exact codec mv");
    check(b.quality == 1.0f, tag + " covered -> quality 1.0");
  }
}

// --- Densifier: partial coverage -> explicit no-coverage cells ---

void testDensifyPartialCoverage() {
  BlockDensifier dens(8, 16, 16);
  std::vector<AnvilMv> mvs = {block(0, 0, 8, 16, 1.0f, 0.0f)};  // left half only
  MotionField f = dens.densify(mvs, 16, 16);

  check(f.gridWidth == 2 && f.gridHeight == 2 && f.blocks.size() == 4, "grid shape 2x2");

  const BlockVec* left0 = cellAt(f, 0, 0);
  const BlockVec* left1 = cellAt(f, 0, 1);
  const BlockVec* right0 = cellAt(f, 1, 0);
  const BlockVec* right1 = cellAt(f, 1, 1);
  check(left0 && left0->mv.x == 1.0f && left0->mv.y == 0.0f && left0->quality == 1.0f,
        "left-top covered: exact mv, quality 1");
  check(left1 && left1->mv.x == 1.0f && left1->quality == 1.0f, "left-bottom covered");
  check(right0 && right0->mv.x == 0.0f && right0->mv.y == 0.0f && right0->quality == 0.0f,
        "right-top uncovered: zero mv, quality 0 (explicit no-coverage)");
  check(right1 && right1->mv.x == 0.0f && right1->mv.y == 0.0f && right1->quality == 0.0f,
        "right-bottom uncovered: zero mv, quality 0");
}

// --- Densifier: discontinuity preservation (no bleed between blocks) ---

void testDensifyDiscontinuity() {
  BlockDensifier dens(8, 16, 16);
  // Two adjacent codec blocks with sharply different motion.
  std::vector<AnvilMv> mvs = {
      block(0, 0, 8, 16, 1.0f, 1.0f),
      block(8, 0, 8, 16, -3.0f, 2.0f),
  };
  MotionField f = dens.densify(mvs, 16, 16);

  const BlockVec* l = cellAt(f, 0, 0);
  const BlockVec* r = cellAt(f, 1, 0);
  const BlockVec* lb = cellAt(f, 0, 1);
  const BlockVec* rb = cellAt(f, 1, 1);
  check(l && r && lb && rb, "all four cells present");
  check(l->mv.x == 1.0f && l->mv.y == 1.0f, "left column keeps block A mv exactly");
  check(r->mv.x == -3.0f && r->mv.y == 2.0f, "right column keeps block B mv exactly");
  check(l->mv.x != r->mv.x, "adjacent cells keep DIFFERENT mvs — no bleed");
  check(lb->mv.x == 1.0f && rb->mv.x == -3.0f, "discontinuity holds on second row too");
  // No blending: neither cell's mv is an average of the two block mvs.
  check(l->mv.x != (1.0f + -3.0f) / 2.0f && r->mv.x != (1.0f + -3.0f) / 2.0f,
        "no cross-block averaging");
  check(l->quality == 1.0f && r->quality == 1.0f, "both cells covered -> quality 1");
}

// --- Densifier: per-block assignment via cell centers, tiled blocks ---

void testDensifyCenterAssignmentTiled() {
  BlockDensifier dens(8, 16, 16);
  // Four 8x8 codec blocks tiled 2x2, each with a distinct mv. Center of grid
  // cell (gx,gy) is (gx*8+4, gy*8+4) — must land in its own tile only.
  std::vector<AnvilMv> mvs = {
      block(0, 0, 8, 8, 1.0f, 0.0f),
      block(8, 0, 8, 8, 2.0f, 0.0f),
      block(0, 8, 8, 8, 3.0f, 0.0f),
      block(8, 8, 8, 8, 4.0f, 0.0f),
  };
  MotionField f = dens.densify(mvs, 16, 16);
  const float expectX[2][2] = {{1.0f, 2.0f}, {3.0f, 4.0f}};
  for (int gy = 0; gy < 2; ++gy) {
    for (int gx = 0; gx < 2; ++gx) {
      const BlockVec* c = cellAt(f, gx, gy);
      check(c && c->mv.x == expectX[gy][gx] && c->quality == 1.0f,
            "tiled block containing center (" + std::to_string(gx) + "," +
                std::to_string(gy) + ") wins with exact mv");
    }
  }
}

// --- Grid invariants: row-major order, ceil sizing, edge cells ---

void testGridInvariants() {
  BlockDensifier dens(8, 64, 64);
  MotionField f = dens.densify({}, 20, 18);  // no codec blocks at all

  check(f.gridWidth == 3, "ceil(20/8) = 3 columns");
  check(f.gridHeight == 3, "ceil(18/8) = 3 rows");
  check(static_cast<int>(f.blocks.size()) == f.gridWidth * f.gridHeight,
        "blocks.size() == gridWidth*gridHeight");
  for (size_t i = 0; i < f.blocks.size(); ++i) {
    const BlockVec& b = f.blocks[i];
    const std::string tag = "blocks[" + std::to_string(i) + "]";
    check(b.gx == static_cast<int>(i) % f.gridWidth, tag + " row-major gx");
    check(b.gy == static_cast<int>(i) / f.gridWidth, tag + " row-major gy");
    check(b.mv.x == 0.0f && b.mv.y == 0.0f && b.quality == 0.0f,
          tag + " no coverage anywhere -> zero mv, quality 0");
  }
  // 1:1 scale sanity: a block covering only the exact center point region of
  // cell (1,1) — center (12,12) — is picked up; neighbors are not.
  std::vector<AnvilMv> mvs = {block(12, 12, 1, 1, 7.0f, 8.0f)};
  MotionField g = dens.densify(mvs, 20, 18);
  const BlockVec* mid = cellAt(g, 1, 1);
  check(mid && mid->mv.x == 7.0f && mid->mv.y == 8.0f && mid->quality == 1.0f,
        "1x1 block exactly at cell center (12,12) covers cell (1,1)");
  check(g.blocks.size() == 9, "grid shape unchanged with blocks present");
  int covered = 0;
  for (const BlockVec& b : g.blocks) covered += (b.quality == 1.0f) ? 1 : 0;
  check(covered == 1, "exactly one cell covered by the 1x1 block");
}

}  // namespace

int main() {
  testClassifierTable();
  testDensifyFullCoverage();
  testDensifyPartialCoverage();
  testDensifyDiscontinuity();
  testDensifyCenterAssignmentTiled();
  testGridInvariants();

  if (g_failures != 0) {
    std::fprintf(stderr, "codecs_tests: %d failure(s)\n", g_failures);
    return 1;
  }
  std::fprintf(stdout, "codecs_tests: all tests passed\n");
  return 0;
}
