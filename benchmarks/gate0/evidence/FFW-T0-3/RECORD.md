# FFW-T0-3 — Generation and state tracing evidence

**Date:** 2026-09-13 · **Task:** Gate-0 R3 · **Status:** complete

## What was added (trace-only, no behavior change)

1. `GpuImageUploader::allocationGeneration()` — epoch incremented only on a
   real (re)allocation (`src/render/GpuImageUploader.hpp/.cpp`).
2. `PlaybackEngine::temporalResetCount_` — cumulative reset generation
   advanced only when a reset frame fully committed (failed dispatch rolls
   the reset back and must not advance the generation).
3. `PlaybackEngine::fsr4MotionProducerLabel_` — per-frame producer identity
   (estimator mode + payload ablation + dense replay).
4. Event trace schema extension (`temporal_forge.event_trace.v1`): every
   dumped frame now carries `stateGenerations` (seekGeneration, sceneCutCount,
   historyResetCount, temporalResetCount, uploaderAllocationGeneration,
   motionProducer) and `resourceGeometry` (model/output W×H).

## Live validation (scene-cut capture)

Input: `two_scene.mp4` (SMPTE bars 1.0 s → black 1.0 s, 640×360@30, cut at
decoder frame 30). Warmup 24, dumped frames 24–33, native INT8 path
(ultraperf_1080) on RX 7900 GRE. Per-frame record:

```text
frame 0000-0005 (dec 24-29): reset=False cut=0 hres=0 tres=1 alloc=1 producer=codec_refined
frame 0006      (dec 30):    reset=True  cut=1 hres=1 tres=2 cause=detector_scene_cut
frame 0007-0009 (dec 31-33): reset=False cut=1 hres=1 tres=2 alloc=1 (stable)
```

Observed invariants: generations advance exactly at the state boundary and are
stable elsewhere; geometry is constant within the run; producer identity is
recorded per frame. `temporalResetCount` starts at 1 because the very first
dispatch is itself a reset (history := current). A stale or mismatched state
would appear as an unexpected generation/geometry change inside a run or a
mismatch between the dumped frame and its consumed state.

## Fail-closed precedents verified by existing contract tests

- `TemporalFrameContinuity::needsReset` — any frame index not exactly last+1
  forces reset (tests: `temporal_frame_continuity_tests`).
- Seek quarantine of pending pre-seek decoded frames, seek-generation dense
  replay rebasing (`tests/test_temporal_runner_contract.py`,
  `tests/fsr4_motion_contract_tests.cpp`).
- Dense replay sidecar dimension mismatch fails closed
  (`tests/fsr4_motion_contract_tests.cpp`).
- New source contracts: `tests/test_gate0_state_generation_trace.py` (5 pass).

## Test state

- Full runnable CTest: 23/23 pass (5 skipped/disabled by configuration).
- Python contracts: 303 passed; 3 pre-existing failures confirmed identical on
  the clean tree (`git stash` check) and unrelated to Gate-0 changes:
  `test_m6_quality_class_annotations` and two `test_review_harness_contract`
  tests — the merged branch retains `review_harness/images/*.png` payloads that
  the M6 "image-payloads-purged" retention contract expects absent. Recorded as
  a Gate-0 finding for the quality-campaign authority; not repaired here.

## Uncertainties

- Live seek and live render-size-change captures (which would show
  `seekGeneration` and `uploaderAllocationGeneration` advancing mid-run) are
  not reachable headlessly (seek is QML-invoked; playlist advance is skipped in
  headless benchmark mode). Coverage is by contract tests + the scene-cut live
  capture; live seek evidence remains open, matching the historical
  "live seek capture remains open" record.
