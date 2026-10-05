// ANVIL stage interfaces (reconciliation 3.5).
//
// Contract rules:
//   - The RUNNER composes stages; no stage calls another stage's interface.
//     That is the cycle-free guarantee.
//   - Every stage has a bypass/null implementation and an oracle twin selected
//     by config; every consumed oracle is recorded in the manifest arm list.
//   - Stages are pure with respect to their inputs in deterministic mode: no
//     atomics, no RNG, fixed iteration order, no hidden irrecoverable state
//     (any state a stage keeps must be resettable via onReset).
#pragma once

#include "anvil/core/Types.hpp"
#include "anvil/core/Window.hpp"

#include <memory>
#include <string>

namespace anvil {

// Normalized side information feeding correspondence. Null: returns the input
// unchanged with provenance preserved (normalization is optional by nature).
struct NormalizedSideInfo {
  SideInfo side;                      // provenance + coverage carried through
  // Densified form, if a normalizer produced one. The RAW side info above is
  // always kept alongside (guardrail: raw side information survives).
  std::shared_ptr<MotionField> densifiedPrior;  // may be null
};

class SideInfoNormalizer {
 public:
  virtual ~SideInfoNormalizer() = default;
  virtual std::string name() const = 0;
  virtual NormalizedSideInfo run(const FrameWindow& window, const NormalizedSideInfo& in) = 0;
  virtual void onReset() {}
};

class CoarseCorrespondence {
 public:
  virtual ~CoarseCorrespondence() = default;
  virtual std::string name() const = 0;
  // Produce the coarse prior for the CENTER frame's alignment to each
  // neighbor. Null bypass: returns a zero-motion field labeled Estimated with
  // quality 0 (declared no-op, testable as the zero-motion control arm).
  virtual MotionField run(const FrameWindow& window, const NormalizedSideInfo& side) = 0;
  virtual void onReset() {}
};

class ResidualRefiner {
 public:
  virtual ~ResidualRefiner() = default;
  virtual std::string name() const = 0;
  // Refine a coarse prior toward image evidence. Null bypass: returns the
  // seed unchanged.
  virtual MotionField run(const FrameWindow& window, const MotionField& seed) = 0;
  virtual void onReset() {}
};

class VisibilityEstimator {
 public:
  virtual ~VisibilityEstimator() = default;
  virtual std::string name() const = 0;
  virtual Visibility run(const FrameWindow& window, const MotionField& motion) = 0;
  virtual void onReset() {}
};

class SampleGeometryEstimator {
 public:
  virtual ~SampleGeometryEstimator() = default;
  virtual std::string name() const = 0;
  // Default implementation must return Status::Unknown — no invented jitter.
  virtual SampleGeometry run(const FrameWindow& window, const MotionField& motion) = 0;
  virtual void onReset() {}
};

// Input bundle handed to reconstruction (composed by the runner).
struct StageBundle {
  WorkingImage center;                        // center observation in working space
  std::vector<WorkingImage> neighbors;        // past then future, working space
  std::vector<int64_t> neighborFrameIndices;  // past then future
  MotionField motion;                         // center->neighbors (grid space)
  Visibility visibility;
  SampleGeometry geometry;
};

class TemporalReconstructor {
 public:
  virtual ~TemporalReconstructor() = default;
  virtual std::string name() const = 0;
  virtual ReconstructionResult run(const FrameWindow& window, const StageBundle& bundle) = 0;
  virtual void onReset() {}
};

// Output backend. Contract shape inherited from the historical
// ITemporalUpscalerBackend lifecycle (create-free; lifetime owned by runner):
// no jitter fields, no fake depth, no FSR ratios. v1: null + passthrough.
class OutputBackend {
 public:
  virtual ~OutputBackend() = default;
  virtual std::string name() const = 0;
  // Convert working-space reconstruction to declared output space. Scale 1:1
  // in v1 (no fixed ratio assumption); later backends may scale explicitly.
  virtual WorkingImage present(const ReconstructionResult& result) = 0;
  virtual void onReset() {}
};

}  // namespace anvil
