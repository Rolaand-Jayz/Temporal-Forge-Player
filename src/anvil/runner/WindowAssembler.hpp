// ANVIL runner — sliding-window assembly state machine.
//
// Extracted verbatim from the inline buffer in src/anvil/runner/main.cpp so
// the seek/reset/discontinuity contract is unit-testable (runner worker
// risk #1; BUILD_READY T12/E6). Pure and deterministic: no RNG, no atomics,
// no iteration over unordered containers — output depends only on the fed
// Observation sequence (reconciliation 3.7).
//
// Contract (preserves main.cpp's behavior on the clean-stream path):
//   - A FrameWindow is emitted only when the full [past + center + future]
//     span of CONTIGUOUS, valid frame indices is buffered in display order.
//   - Frames at stream start lacking past neighbors are counted in
//     skippedWarmup; frames at end of stream lacking future neighbors in
//     skippedTrailing (endOfStream()).
//   - An invalid observation (valid=false), a frame-index discontinuity, or
//     a source seek-generation change FLUSHES the buffer, emits a
//     ResetEvent{cause, frameIndex} and restarts buffering. Frames held in
//     the buffer at such a break are counted in skippedDiscontinuity —
//     distinct from warmup/trailing.
//   - A mid-stream change of plane dimensions or pixel-format class is ALSO
//     a reset boundary (cause=Dimensions). v1 has no rescale path: the
//     runner must fail the run loudly (exit 4) on this cause; silent
//     mixed-dimension output is forbidden.
#pragma once

#include "anvil/core/Types.hpp"
#include "anvil/core/Window.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace anvil {

enum class ResetCause : int {
  Invalid = 0,        // observation arrived with valid == false
  Discontinuity = 1,  // frame index skipped within one seek generation
  Seek = 2,           // ObservationSource generation changed
  Dimensions = 3,     // plane dimensions / pixel-format class changed mid-stream
};

// Stable manifest token for a cause ("invalid" | "discontinuity" | "seek" |
// "dimensions"); used verbatim in the run manifest's "resets" array.
const char* resetCauseName(ResetCause cause);

struct ResetEvent {
  ResetCause cause = ResetCause::Invalid;
  uint64_t frameIndex = 0;  // fileFrameIndex of the observation that broke the run
  std::string detail;       // non-empty for Dimensions: "WxHxD -> W'xH'D'"
};

struct WindowAssemblerStats {
  int64_t skippedWarmup = 0;         // stream-start frames lacking past neighbors
  int64_t skippedTrailing = 0;       // end-of-stream frames lacking future neighbors
  int64_t skippedDiscontinuity = 0;  // buffered frames discarded at a reset boundary
};

struct PushResult {
  std::vector<FrameWindow> windows;  // 0 or 1 complete windows
  std::vector<ResetEvent> resets;    // 0..2 events (seek + invalid can co-occur)
};

class WindowAssembler {
 public:
  WindowAssembler(int past, int future);

  // Feed one observation in display order. sourceGeneration is the
  // ObservationSource generation observed at read time; the assembler treats
  // a change (after the first ever push) as a Seek reset boundary. The
  // breaking observation itself is never buffered on invalid/dimension
  // boundaries; on seek/discontinuity boundaries it continues into the new
  // run (preserving the runner's former post-flush behavior).
  PushResult push(Observation obs, uint32_t sourceGeneration);

  // End of stream: buffered frames never became centers — the first `past`
  // lack past neighbors (warmup), the rest lack future neighbors (trailing).
  void endOfStream();

  const WindowAssemblerStats& stats() const;
  uint32_t generation() const { return generation_; }
  size_t buffered() const { return buf_.size(); }

 private:
  // Discard the buffer as a reset boundary: buffered frames count into
  // skippedDiscontinuity; contiguity and the dimension reference re-arm.
  void flushForReset();

  int past_;
  int future_;
  std::deque<std::shared_ptr<Observation>> buf_;
  uint64_t expectedNext_ = 0;
  bool haveExpected_ = false;
  bool seenAnyPush_ = false;  // first-ever push adopts the generation silently
  uint32_t generation_ = 0;
  bool haveDimRef_ = false;   // per-run reference for the dimensions check
  int refWidth_ = 0;
  int refHeight_ = 0;
  int refBitDepth_ = 0;
  WindowAssemblerStats stats_;
};

}  // namespace anvil
