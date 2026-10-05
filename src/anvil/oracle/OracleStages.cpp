// ANVIL oracle stage twins — implementation. See OracleStages.hpp for the
// contract (record keys, alignment, loud-failure rule).
#include "anvil/oracle/OracleStages.hpp"
#include "anvil/oracle/SidecarOracle.hpp"  // concrete source: lastError() detail on failure

#include <stdexcept>
#include <string>
#include <vector>

namespace anvil {

namespace {

// Common loud-failure helper: a missing/mismatched oracle record must fail
// the run, never degrade silently (PROJECT DECISION).
[[noreturn]] void throwOracleError(const std::string& stage, int64_t targetIndex,
                                   int windowOffset, const std::string& detail) {
  throw std::runtime_error("oracle " + stage + ": failed for target=" +
                           std::to_string(targetIndex) + " windowOffset=" +
                           std::to_string(windowOffset) + ": " + detail);
}

}  // namespace

OracleCoarse::OracleCoarse(std::shared_ptr<OracleDataSource> source)
    : source_(std::move(source)) {}

std::string OracleCoarse::name() const { return "oracle"; }

std::vector<MotionField> OracleCoarse::run(const FrameWindow& window,
                                           const NormalizedSideInfo& /*side*/) {
  const Observation& center = window.center();
  std::vector<MotionField> out;
  out.reserve(window.observations.size() - 1);
  // Past-then-future, matching the window's observation order minus center.
  for (size_t i = 0; i < window.observations.size(); ++i) {
    if (i == window.centerIdx) continue;
    const Observation& neighbor = *window.observations[i];
    MotionField field;
    const int offset = neighbor.windowOffset;
    if (!source_->loadFlow(int64_t(center.id.fileFrameIndex), offset, field)) {
      std::string detail = "loadFlow failed";
      if (const SidecarOracleSource* sidecar =
              dynamic_cast<const SidecarOracleSource*>(source_.get())) {
        detail = sidecar->lastError();
      }
      throwOracleError("coarse", int64_t(center.id.fileFrameIndex), offset, detail);
    }
    out.push_back(std::move(field));
  }
  return out;
}

OracleVisibilityEstimator::OracleVisibilityEstimator(std::shared_ptr<OracleDataSource> source)
    : source_(std::move(source)) {}

std::string OracleVisibilityEstimator::name() const { return "oracle"; }

std::vector<Visibility> OracleVisibilityEstimator::run(
    const FrameWindow& window, const std::vector<MotionField>& /*motion*/) {
  const Observation& center = window.center();
  std::vector<Visibility> out;
  out.reserve(window.observations.size() - 1);
  for (size_t i = 0; i < window.observations.size(); ++i) {
    if (i == window.centerIdx) continue;
    const Observation& neighbor = *window.observations[i];
    Visibility vis;
    const int offset = neighbor.windowOffset;
    if (!source_->loadVisibility(int64_t(center.id.fileFrameIndex), offset, vis)) {
      std::string detail = "loadVisibility failed";
      if (const SidecarOracleSource* sidecar =
              dynamic_cast<const SidecarOracleSource*>(source_.get())) {
        detail = sidecar->lastError();
      }
      throwOracleError("visibility", int64_t(center.id.fileFrameIndex), offset, detail);
    }
    out.push_back(std::move(vis));
  }
  return out;
}

OracleGeometryEstimator::OracleGeometryEstimator(std::shared_ptr<OracleDataSource> source)
    : source_(std::move(source)) {}

std::string OracleGeometryEstimator::name() const { return "oracle"; }

SampleGeometry OracleGeometryEstimator::run(
    const FrameWindow& window, const std::vector<MotionField>& /*motion*/) {
  const Observation& center = window.center();
  SampleGeometry geo;
  const int64_t targetIndex = int64_t(center.id.fileFrameIndex);
  if (!source_->loadGeometry(targetIndex, geo)) {
    std::string detail = "loadGeometry failed";
    if (const SidecarOracleSource* sidecar =
            dynamic_cast<const SidecarOracleSource*>(source_.get())) {
      detail = sidecar->lastError();
    }
    throwOracleError("geometry", targetIndex, 0, detail);
  }
  return geo;
}

}  // namespace anvil
