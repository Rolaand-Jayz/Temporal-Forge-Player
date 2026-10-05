// ANVIL temporal window: configurable past/future support around a center
// target. Causal-only is an arm (past=0, future=0), not an assumption.
// The window is assembled by the runner on frame indices; VFR safety bounds
// the window by PTS range (decision deferred with fixture evidence —
// reconciliation 3.9 open questions).
#pragma once

#include "anvil/core/Types.hpp"

#include <memory>
#include <vector>

namespace anvil {

struct WindowConfig {
  int past = 0;    // number of past observations relative to target
  int future = 0;  // number of future observations relative to target
};

// One assembled window: center target plus past/future neighbors in display
// order. Observations are shared (runner-owned buffer); stages must not
// mutate them.
struct FrameWindow {
  std::vector<std::shared_ptr<const Observation>> observations;  // past..future
  size_t centerIdx = 0;  // index of the target observation within observations

  const Observation& center() const { return *observations[centerIdx]; }

  std::vector<std::shared_ptr<const Observation>> past() const {
    return std::vector<std::shared_ptr<const Observation>>(
        observations.begin(), observations.begin() + static_cast<long>(centerIdx));
  }
  std::vector<std::shared_ptr<const Observation>> future() const {
    return std::vector<std::shared_ptr<const Observation>>(
        observations.begin() + static_cast<long>(centerIdx) + 1, observations.end());
  }
};

}  // namespace anvil
