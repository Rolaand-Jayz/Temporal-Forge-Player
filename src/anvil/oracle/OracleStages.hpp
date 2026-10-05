// ANVIL oracle stage twins (reconciliation 3.5/3.7): oracle implementations
// of the CoarseCorrespondence / VisibilityEstimator / SampleGeometryEstimator
// stage interfaces, backed by an OracleDataSource (e.g. SidecarOracleSource).
//
// PROJECT DECISION: these classes are constructed by the runner DIRECTLY when
// the pipeline config selects "oracle" for coarse/visibility/geometry — they
// are intentionally NOT registered in StageFactories, because an oracle arm
// requires an explicitly configured OracleDataSource (the manifest arm list
// records every consumed oracle).
//
// Record key: (targetIndex = center observation's fileFrameIndex,
// windowOffset = the neighbor observation's windowOffset field; negative =
// past). Per-neighbor output arrays are aligned PAST-THEN-FUTURE, matching
// the window's observation order (everything except the center).
//
// Missing required oracle record is LOUD: run() throws std::runtime_error and
// the runner fails the run. No silent defaults, no zero-motion fallback.
#pragma once

#include "anvil/oracle/Oracle.hpp"
#include "anvil/stages/Stages.hpp"

#include <memory>

namespace anvil {

// Oracle coarse correspondence: returns the sidecar flow field for every
// window neighbor (past then future), src = Src::Oracle.
class OracleCoarse : public CoarseCorrespondence {
 public:
  explicit OracleCoarse(std::shared_ptr<OracleDataSource> source);
  std::string name() const override;
  std::vector<MotionField> run(const FrameWindow& window,
                               const NormalizedSideInfo& side) override;

 private:
  std::shared_ptr<OracleDataSource> source_;
};

// Oracle visibility: returns the sidecar visibility map for every window
// neighbor (past then future), src = Src::Oracle.
class OracleVisibilityEstimator : public VisibilityEstimator {
 public:
  explicit OracleVisibilityEstimator(std::shared_ptr<OracleDataSource> source);
  std::string name() const override;
  std::vector<Visibility> run(const FrameWindow& window,
                              const std::vector<MotionField>& motion) override;

 private:
  std::shared_ptr<OracleDataSource> source_;
};

// Oracle sample geometry: returns the sidecar affine for the center target
// with Status::Known (the ONLY sanctioned path to Known — nothing is invented).
class OracleGeometryEstimator : public SampleGeometryEstimator {
 public:
  explicit OracleGeometryEstimator(std::shared_ptr<OracleDataSource> source);
  std::string name() const override;
  SampleGeometry run(const FrameWindow& window,
                     const std::vector<MotionField>& motion) override;

 private:
  std::shared_ptr<OracleDataSource> source_;
};

}  // namespace anvil
