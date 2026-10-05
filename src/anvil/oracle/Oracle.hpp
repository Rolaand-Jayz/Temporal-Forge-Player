// ANVIL oracle injection (reconciliation 3.7).
// OracleSources implement the SAME stage interfaces as estimators and are
// selected per arm by config; every consumed oracle is recorded in the run
// manifest's arm list. Oracle data comes from fixture sidecars (deterministic
// fixtures generated at build time carry HR ground truth + exact flow).
#pragma once

#include "anvil/core/Types.hpp"
#include "anvil/core/Window.hpp"

#include <memory>
#include <string>

namespace anvil {

// A ground-truth record source keyed by (fixtureFrameIndex, neighborOffset).
// Sidecar format: one JSON index + raw .npy/.f32 payload files (same writer
// as capture/, so oracle artifacts and stage dumps diff with one tool).
class OracleDataSource {
 public:
  virtual ~OracleDataSource() = default;
  virtual bool open(const std::string& sidecarPath) = 0;
  virtual bool loadFlow(int64_t targetIndex, int windowOffset, MotionField& out) = 0;
  virtual bool loadVisibility(int64_t targetIndex, int windowOffset, Visibility& out) = 0;
  virtual bool loadGeometry(int64_t targetIndex, SampleGeometry& out) = 0;
  virtual bool loadGroundTruth(int64_t targetIndex, SourcePlanes& out, ColorMeta& meta) = 0;
};

}  // namespace anvil
