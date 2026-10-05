// ANVIL runner — sliding-window assembly state machine (see WindowAssembler.hpp
// for the contract). Determinism: fixed branch order, no unordered iteration,
// no RNG — the emitted window/reset sequence is a pure function of the fed
// observations.
#include "anvil/runner/WindowAssembler.hpp"

#include <algorithm>
#include <utility>

namespace anvil {

const char* resetCauseName(ResetCause cause) {
  switch (cause) {
    case ResetCause::Invalid: return "invalid";
    case ResetCause::Discontinuity: return "discontinuity";
    case ResetCause::Seek: return "seek";
    case ResetCause::Dimensions: return "dimensions";
  }
  return "unknown";
}

WindowAssembler::WindowAssembler(int past, int future) : past_(past), future_(future) {}

// Trailing return type: out-of-class return types cannot be qualified with
// WindowAssembler:: because PushResult is a namespace-scope type.
auto WindowAssembler::push(Observation obs, uint32_t sourceGeneration) -> PushResult {
  PushResult out;
  // --- seek-generation boundary (was: source.generation() != runGeneration
  // in main.cpp). The very first push adopts whatever generation the source
  // reports without an event: the old runner's pre-buffer generation flushes
  // on frames below cfg.firstFrame were no-ops on an always-empty buffer.
  if (sourceGeneration != generation_) {
    if (seenAnyPush_) {
      ResetEvent ev;
      ev.cause = ResetCause::Seek;
      ev.frameIndex = obs.id.fileFrameIndex;
      out.resets.push_back(std::move(ev));
      flushForReset();
    }
    generation_ = sourceGeneration;
  }

  // --- invalid-observation boundary (was: !obs.id.valid -> flushRun). Decode
  // errors/concealment break temporal continuity; the observation's planes
  // are untrustworthy and never enter a window.
  seenAnyPush_ = true;
  if (!obs.id.valid) {
    ResetEvent ev;
    ev.cause = ResetCause::Invalid;
    ev.frameIndex = obs.id.fileFrameIndex;
    out.resets.push_back(std::move(ev));
    flushForReset();
    return out;
  }

  // --- index-discontinuity boundary (was: haveExpected && index !=
  // expectedNext). Non-contiguous display order breaks window adjacency.
  if (haveExpected_ && obs.id.fileFrameIndex != expectedNext_) {
    ResetEvent ev;
    ev.cause = ResetCause::Discontinuity;
    ev.frameIndex = obs.id.fileFrameIndex;
    out.resets.push_back(std::move(ev));
    flushForReset();
  }
  haveExpected_ = true;
  expectedNext_ = obs.id.fileFrameIndex + 1;

  // --- dimensions / pixel-format-class boundary (NEW; v1 has no rescale
  // path). All observations are normalized to 8-bit 4:2:0 by the source
  // (ObservationSource.cpp:334-336), so color.bitDepth is the only
  // observable format-class axis; plane extents are the dimension axis.
  if (haveDimRef_ && (obs.planes.width != refWidth_ || obs.planes.height != refHeight_ ||
                      obs.color.bitDepth != refBitDepth_)) {
    ResetEvent ev;
    ev.cause = ResetCause::Dimensions;
    ev.frameIndex = obs.id.fileFrameIndex;
    ev.detail = std::to_string(refWidth_) + "x" + std::to_string(refHeight_) + "x" +
                std::to_string(refBitDepth_) + " -> " + std::to_string(obs.planes.width) +
                "x" + std::to_string(obs.planes.height) + "x" +
                std::to_string(obs.color.bitDepth);
    out.resets.push_back(std::move(ev));
    flushForReset();
    return out;  // runner fails the run loudly on this cause; never buffered
  }
  if (!haveDimRef_) {
    haveDimRef_ = true;
    refWidth_ = obs.planes.width;
    refHeight_ = obs.planes.height;
    refBitDepth_ = obs.color.bitDepth;
  }

  // --- buffer one observation; emit when the full span is contiguous.
  buf_.push_back(std::make_shared<Observation>(std::move(obs)));
  const size_t span = static_cast<size_t>(past_) + static_cast<size_t>(future_) + 1;
  if (buf_.size() < span) return out;

  // windowOffset convention (Types.hpp / sidecar records): thisIndex -
  // targetIndex, negative for past neighbors. Contiguity makes the offset
  // purely positional: array index k sits at offset k - past.
  for (size_t k = 0; k < buf_.size(); ++k) {
    buf_[k]->windowOffset = static_cast<int>(k) - past_;
  }
  FrameWindow window;
  window.centerIdx = static_cast<size_t>(past_);
  window.observations.assign(buf_.begin(), buf_.end());
  buf_.pop_front();
  out.windows.push_back(std::move(window));
  return out;
}

void WindowAssembler::endOfStream() {
  // Frames left in the buffer never became centers: the first `past` of them
  // lack past neighbors (warmup), the rest lack future neighbors (trailing).
  // Both are skipped and counted, never half-windowed.
  const int64_t remaining = static_cast<int64_t>(buf_.size());
  if (remaining > 0) {
    const int64_t warm = std::min<int64_t>(past_, remaining);
    stats_.skippedWarmup += warm;
    stats_.skippedTrailing += remaining - warm;
  }
  buf_.clear();
}

const WindowAssemblerStats& WindowAssembler::stats() const { return stats_; }

void WindowAssembler::flushForReset() {
  stats_.skippedDiscontinuity += static_cast<int64_t>(buf_.size());
  buf_.clear();
  haveExpected_ = false;
  haveDimRef_ = false;  // the next observation re-establishes the run reference
}

}  // namespace anvil
