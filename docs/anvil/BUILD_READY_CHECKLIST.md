# BUILD_READY_FOR_RESEARCH — qualification checklist

**Purpose:** Phase-5 instrument. Every mandatory condition from the execution pack's
`BUILD_READY_CONTRACT.md` is mapped to the concrete evidence that will satisfy it. This file
is updated as evidence lands; the qualification commit fills every row with a MEASURED
result or an explicit not-applicable adjudication. No condition may be marked satisfied
without a command, test, or artifact that can be re-run.

## Mandatory architecture conditions

| # | Condition | Evidence source | Status |
|---|---|---|---|
| A1 | Successor path active and isolated from the closed FSR campaign | `anvil_runner` target builds and runs from `src/anvil/` only; `src/anvil` excluded from the FSR-era GLOB (CMakeLists.txt); zero FSR includes under src/anvil (grep evidence) | pending |
| A2 | Runs without proprietary/RE-derived FSR assets | clean-clone build + run with no `TFORGE_FSR4_RE_ROOT`, no weight blobs; grep: no WeightBlob/Fsr4Paths refs in src/anvil | pending |
| A3 | FSR optional, not a core dependency | anvil_core links only FFmpeg+Threads (CMake evidence); full ctest green with FSR1 probe OFF (default) | pending |
| A4 | Past/future temporal window configurable | PipelineConfig.past/future; window tests; manifest records window | pending |
| A5 | Single-frame control exists | `reconstruct: "center"` arm; test asserts output == center | pending |
| A6 | Major stages independently selectable/bypassable | StageFactories names incl. null/bypass variants; config arms evidence | pending |
| A7 | Missing side information represented explicitly, not fabricated | SideInfo.Provenance/Coverage enum; codecs tests; zero-quality no-coverage grid cells | pending |
| A8 | Backend-neutral confidence/visibility | Visibility struct with src field; unity/photo_fwbw/oracle implementations | pending |
| A9 | Sample geometry known/estimated/unknown, no forced fake jitter | SampleGeometry.Status default Unknown; no jitter code in src/anvil (grep evidence) | pending |
| A10 | Temporal reconstruction produces output through a non-FSR path | weighted_blend run on fixture clip end-to-end; output npy artifact | pending |

## Mandatory testability conditions

| # | Condition | Evidence source | Status |
|---|---|---|---|
| T1 | Headless/offline deterministic runner exists | `anvil_runner --config` runs with no display/Qt (process evidence) | pending |
| T2 | Exact input frame indices/timestamps selectable | config firstFrame/lastFrame; observations carry ptsUs; manifest frameRange | pending |
| T3 | Exact configuration serialized in a run manifest | run_manifest.json config.canonicalJson + sha256 | pending |
| T4 | Git/build/input provenance captured | manifest git/binary/input hashes | pending |
| T5 | Per-stage timing hooks exist | timings.json per run | pending |
| T6 | Intermediate dumps exist for every consequential stage | captureStages mechanism; capture_index.json evidence | pending |
| T7 | Oracle correspondence replaces estimated | coarse:"oracle" arm on fixture sidecar | pending |
| T8 | Oracle visibility replaces estimated | visibility:"oracle" arm | pending |
| T9 | Oracle sample geometry/phase replaces estimates | geometry:"oracle" arm | pending |
| T10 | HR ground truth attachable to synthetic fixtures | make_fixtures.py gt_*.npy + oracle ground_truth records | pending |
| T11 | Stochastic components seed-recorded or disabled | no RNG in src/anvil (grep); seed recorded in manifest | pending |
| T12 | Scene cut/reset observable and controllable | window reset on generation change/discontinuity; manifest counts; onReset chain | pending |

## Mandatory codec conditions (H.264, HEVC, AV1)

| # | Condition | Evidence source | Status |
|---|---|---|---|
| C1 | Capability detected explicitly | CodecSideInfoClassifier table (MEASURED: h264 yes; hevc/av1 no; any hwaccel no) | pending |
| C2 | Actual encoded-MV availability proven or marked unsupported | codec audit measurements + classifier tests | pending |
| C3 | Precision/scale/partition/reference semantics normalized where available | AnvilMv normalization (source px, destination_plus_motion, quarter-pel origin documented) | pending |
| C4 | Ambiguous reference data cannot silently enter reconstruction | direction field only; bFrame marker without identity claims; zero-quality uncovered cells | pending |
| C5 | Hardware/software decode behavior separately documented | Coverage::HwDecodeNoExport; obs source forces sw for codec MVs | pending |
| C6 | Fallback works when side information unavailable | coarse:"blocksearch" arm (estimator, labeled Estimated) on any codec | pending |

## Mandatory color conditions

| # | Condition | Evidence source | Status |
|---|---|---|---|
| K1 | Range/primaries/transfer/matrix/chroma siting tracked when present | ColorMeta carried at decode; obs tests | pending |
| K2 | Working-space conversion explicit | convertToWorking declared-space record; unspecified+no-override fails loudly | pending |
| K3 | SDR/HDR do not silently share wrong transfer assumptions | v1 declares display-referred space in every manifest/capture (reconciliation 3.8) | pending |
| K4 | Metrics/captures know their space | manifest transferOverride/declared space field; capture sidecars | pending |

## Mandatory engineering verification

| # | Condition | Evidence source | Status |
|---|---|---|---|
| E1 | Clean configure/build succeeds | full ninja from scratch in clean build dir (exit 0) | pending |
| E2 | Existing applicable tests pass | ctest full suite green (pre-existing + new) | pending |
| E3 | New unit/integration tests pass | anvil_* ctest targets green | pending |
| E4 | Vulkan validation clean for exercised new paths or adjudicated | v1 successor core is CPU-only: adjudicated not applicable at this gate (no new Vulkan paths exercised); revisit when GPU backend lands | pending |
| E5 | No known resource lifetime/synchronization defect | code review evidence + capture-sink thread join test | pending |
| E6 | Graceful fallback/error reporting tested | error-path tests (missing oracle, unspecified transfer, bad config) | pending |
| E7 | Deterministic replay reproduces same artifacts | `anvil_runner --replay` byte-compare exit 0 on fixture run | pending |
| E8 | Clean-clone instructions sufficient | docs/anvil RUNBOOK section + fresh-clone verification | pending |

## Qualification rule

`BUILD_READY_FOR_RESEARCH = TRUE` only when every row is **satisfied (with re-runnable
evidence)** or **adjudicated not-applicable (with concrete justification)**. Rows marked
pending block the gate. The qualification commit records the final table with results.
