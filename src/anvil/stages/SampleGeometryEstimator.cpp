// ANVIL stage: sample geometry estimation (reconciliation 3.5, ask item 5).
// unknown:      always Status::Unknown — the default (no invented jitter,
//               reconciliation 3.3.1/3.3.8).
// global_xform: least-squares 6-parameter affine from block correspondences
//               with quality > 0.5 across all neighbors; < 8 correspondences
//               or a rank-deficient normal matrix -> Status::Unknown.
#include "anvil/stages/Stages.hpp"
#include "anvil/stages/StageInternal.hpp"
#include "anvil/stages/StageUtil.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace anvil {
namespace {

constexpr size_t kMinCorrespondences = 8;
constexpr double kRankEps = 1e-8;

// Solve the 3x3 normal system M z = rhs by Gaussian elimination with partial
// pivoting. Pivot rule is deterministic: largest |pivot| among remaining
// rows, ties keep the lowest row index. Returns false when rank-deficient.
bool solve3(double m[3][3], const double rhs[3], double out[3]) {
  double a[3][4];
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) a[i][j] = m[i][j];
    a[i][3] = rhs[i];
  }
  for (int col = 0; col < 3; ++col) {
    int piv = col;
    for (int r = col + 1; r < 3; ++r) {
      if (std::abs(a[r][col]) > std::abs(a[piv][col])) piv = r;
    }
    if (std::abs(a[piv][col]) < kRankEps) return false;
    if (piv != col) {
      for (int k = col; k < 4; ++k) std::swap(a[col][k], a[piv][k]);
    }
    for (int r = col + 1; r < 3; ++r) {
      const double f = a[r][col] / a[col][col];
      for (int k = col; k < 4; ++k) a[r][k] -= f * a[col][k];
    }
  }
  for (int i = 2; i >= 0; --i) {
    double s = a[i][3];
    for (int j = i + 1; j < 3; ++j) s -= a[i][j] * out[j];
    out[i] = s / a[i][i];
  }
  return true;
}

class UnknownGeometry final : public SampleGeometryEstimator {
 public:
  std::string name() const override { return "unknown"; }
  SampleGeometry run(const FrameWindow& /*window*/,
                     const std::vector<MotionField>& /*motion*/) override {
    return SampleGeometry{};  // status defaults to Unknown (Types.hpp)
  }
};

class GlobalXformGeometry final : public SampleGeometryEstimator {
 public:
  std::string name() const override { return "global_xform"; }
  SampleGeometry run(const FrameWindow& /*window*/,
                     const std::vector<MotionField>& motion) override {
    // Correspondence: block center -> block center + mv, for blocks with
    // quality > 0.5, neighbors in index order then grid order (fixed).
    struct Corr {
      double x, y, tx, ty;
    };
    std::vector<Corr> cs;
    for (const MotionField& mf : motion) {
      const int gs = mf.gridStep > 0 ? mf.gridStep : 8;
      for (const BlockVec& bv : mf.blocks) {
        if (!(bv.quality > 0.5f)) continue;
        const double px = static_cast<double>(bv.gx * gs) + gs * 0.5;
        const double py = static_cast<double>(bv.gy * gs) + gs * 0.5;
        cs.push_back({px, py, px + static_cast<double>(bv.mv.x),
                      py + static_cast<double>(bv.mv.y)});
      }
    }
    if (cs.size() < kMinCorrespondences) return SampleGeometry{};

    // Normal equations for the [x y 1] design; the two output axes share the
    // same 3x3 moment matrix and are solved independently (the 6 affine
    // parameters factor into two 3-parameter least-squares systems).
    double m[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double rhsX[3] = {0, 0, 0};
    double rhsY[3] = {0, 0, 0};
    for (const Corr& c : cs) {
      const double u[3] = {c.x, c.y, 1.0};
      for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i][j] += u[i] * u[j];
        rhsX[i] += u[i] * c.tx;
        rhsY[i] += u[i] * c.ty;
      }
    }
    double zx[3] = {0, 0, 0};
    double zy[3] = {0, 0, 0};
    if (!solve3(m, rhsX, zx) || !solve3(m, rhsY, zy)) {
      return SampleGeometry{};  // rank-deficient -> Unknown, never fake
    }
    SampleGeometry g;
    g.status = SampleGeometry::Status::Estimated;
    // x' = a*x + b*y + c ; y' = d*x + e*y + f (Mat3f, Types.hpp)
    g.globalXform = Mat3f{static_cast<float>(zx[0]), static_cast<float>(zx[1]),
                          static_cast<float>(zx[2]), static_cast<float>(zy[0]),
                          static_cast<float>(zy[1]), static_cast<float>(zy[2])};
    return g;
  }
};

}  // namespace

std::unique_ptr<SampleGeometryEstimator> makeUnknownGeometry() {
  return std::make_unique<UnknownGeometry>();
}

std::unique_ptr<SampleGeometryEstimator> makeGlobalXformGeometry() {
  return std::make_unique<GlobalXformGeometry>();
}

}  // namespace anvil
