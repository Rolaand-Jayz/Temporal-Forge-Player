// ANVIL oracle sidecar source (concrete OracleDataSource). Sidecar layout
// (written by tools/anvil/make_fixtures.py):
//   <dir>/oracle_index.json  — {"schema":"anvil.oracle.v1","records":[
//                               {"kind":"flow|visibility|geometry|ground_truth",
//                                "targetIndex":N,"windowOffset":K,
//                                "file":"<relative .npy path>", ...extras}]}
//   <dir>/<file>.npy         — payloads in capture/ .npy format:
//       flow:        shape (gridH*gridW*2,) f32 (dy,dx per block, gridStep in extras)
//       visibility:  shape (H,W,) f32 weights (occluded = weight 0)
//       geometry:    shape (6,) f32 affine (a,b,c,d,e,f)
//       ground_truth: shape (3,H,W) f32 RGB, plus optional color meta in record
#pragma once

#include "anvil/oracle/Oracle.hpp"

#include <memory>

namespace anvil {

// Implemented in oracle/SidecarOracle.cpp:
//   open() parses oracle_index.json (hand-rolled minimal JSON reader scoped to
//   this schema); loaders validate shapes/dtypes and return false + set an
//   error string on mismatch (missing oracle data is loud, never defaulted).
class SidecarOracleSource : public OracleDataSource {
 public:
  SidecarOracleSource();
  ~SidecarOracleSource() override;
  bool open(const std::string& sidecarPath) override;
  bool loadFlow(int64_t targetIndex, int windowOffset, MotionField& out) override;
  bool loadVisibility(int64_t targetIndex, int windowOffset, Visibility& out) override;
  bool loadGeometry(int64_t targetIndex, SampleGeometry& out) override;
  bool loadGroundTruth(int64_t targetIndex, SourcePlanes& out, ColorMeta& meta) override;
  const std::string& lastError() const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace anvil
