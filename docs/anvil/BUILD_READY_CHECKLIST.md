# BUILD_READY_FOR_RESEARCH — qualification record

**Qualified:** 2026-10-05, branch `anvil/successor-build` @ commit recorded in
[`FINAL_HANDOFF_20261005.md`](FINAL_HANDOFF_20261005.md). Every row carries re-runnable
evidence (command, test, or artifact). Evidence classes per
[`../closure/EVALUATION_STANDARD.md`](../closure/EVALUATION_STANDARD.md).

## Mandatory architecture conditions

| # | Condition | Evidence | Status |
|---|---|---|---|
| A1 | Successor path active and isolated from the closed FSR campaign | `src/anvil/` tree with own targets; `src/anvil` excluded from the FSR-era GLOB (CMakeLists.txt); `grep -rniE 'fsr\|WeightBlob\|TFORGE_FSR4' src/anvil/` matches only guardrail comments, zero code references | SATISFIED |
| A2 | Runs without proprietary/RE-derived FSR assets | clean-clone build (`git archive HEAD` → fresh dir) builds and runs all tests with no `TFORGE_FSR4_RE_ROOT`, no weight blobs, no FidelityFX SDK | SATISFIED |
| A3 | FSR optional, not a core dependency | `anvil_core` links FFmpeg+Threads only (CMakeLists.txt ANVIL section); all 29 ctest + 13 arms tests green with FSR1 probe OFF (default) | SATISFIED |
| A4 | Past/future temporal window configurable | `PipelineConfig.past/future`; `WindowConfig`; manifest `window` field; window tests (2/1, 0/0, 1/1 arms in `test_anvil_arms_e2e.py`) | SATISFIED |
| A5 | Single-frame control exists | `reconstruct: "center"`; `test_single_frame_control` (16 frames, past=0/future=0) | SATISFIED |
| A6 | Major stages independently selectable/bypassable | `StageFactories` all 7 families with bypass variants; arms suite flips each stage by config | SATISFIED |
| A7 | Missing side information represented explicitly, not fabricated | `SideInfo.Provenance`/`Coverage` enums (`Types.hpp`); classifier tests incl. non-I-empty → Present-with-empty; densifier zero-quality uncovered cells | SATISFIED |
| A8 | Backend-neutral confidence/visibility | `Visibility{src,weight,occluded}` with unity/photo_fwbw/oracle implementations; no backend types involved | SATISFIED |
| A9 | Sample geometry known/estimated/unknown, no forced fake jitter | `SampleGeometry::Status` default Unknown; `grep -r 'Halton\|jitter' src/anvil` → contract comments only | SATISFIED |
| A10 | Temporal reconstruction produces output through a non-FSR path | weighted_blend e2e arms emit 14–16 `.npy` frames from decoded H.264 without any FSR component | SATISFIED |

## Mandatory testability conditions

| # | Condition | Evidence | Status |
|---|---|---|---|
| T1 | Headless/offline deterministic runner exists | `anvil_runner --config` runs with no display; Qt-free target (CMake evidence) | SATISFIED |
| T2 | Exact input frame indices/timestamps selectable | `firstFrame`/`lastFrame` config; `ptsUs` per observation; manifest `frameRange` | SATISFIED |
| T3 | Exact configuration serialized in a run manifest | `run_manifest.json` `config.canonicalJson` + sha256; parser round-trip tests | SATISFIED |
| T4 | Git/build/input provenance captured | manifest `git{head,dirty}`, `binary{path,sha256}` (`/proc/self/exe`), `input{path,sha256}` | SATISFIED |
| T5 | Per-stage timing hooks exist | `timings.json` per run (7 stage keys, verified in e2e capture checks) | SATISFIED |
| T6 | Intermediate dumps exist for every consequential stage | `captureStages` mechanism → per-stage `.npy` + `capture_index.json` (verified in e2e blocksearch arm) | SATISFIED |
| T7 | Oracle correspondence replaces estimated | `coarse:"oracle"` arm — `test_oracle_flow_estimated_confidence`; missing record fails loudly (exit 4, verified) | SATISFIED |
| T8 | Oracle visibility replaces estimated | `visibility:"oracle"` — `test_estimated_flow_oracle_confidence` | SATISFIED |
| T9 | Oracle sample geometry/phase replaces estimates | `geometry:"oracle"` — `test_oracle_geometry` (Status::Known from sidecar) | SATISFIED |
| T10 | HR ground truth attachable to synthetic fixtures | `make_fixtures.py` `gt_*.npy` + sidecar `ground_truth` records; `loadGroundTruth` tests | SATISFIED |
| T11 | Stochastic components seed-recorded or disabled | `grep -r 'rand\|mt19937\|random_device' src/anvil` → zero; `seed` recorded in manifest and replay key | SATISFIED |
| T12 | Scene cut/reset observable and controllable | `WindowAssembler` ResetEvents (invalid/discontinuity/seek/dimensions); manifest `resets[]`; 9 unit tests; generation-change resets wired through `onReset()` | SATISFIED |

## Mandatory codec conditions (H.264, HEVC, AV1)

| # | Condition | Evidence | Status |
|---|---|---|---|
| C1 | Capability detected explicitly | `CodecSideInfoClassifier` measured table (audit dwfrun-015b2ece: h264 export yes; hevc no; av1 no; hw no) | SATISFIED |
| C2 | Actual encoded-MV availability proven or marked unsupported | measured probes (ffprobe + ctypes decode on this stack); classifier unit tests | SATISFIED |
| C3 | Precision/scale/partition/reference semantics normalized where available | `AnvilMv` (source px, destination_plus_motion, quarter-pel origin documented; direction-only identity — API exposes no more; `motion_vector.h` XXX noted) | SATISFIED |
| C4 | Ambiguous reference data cannot silently enter reconstruction | direction-field-only vectors; `bFrame` marker without identity claims; uncovered cells quality 0 | SATISFIED |
| C5 | Hardware/software decode behavior separately documented | `Coverage::HwDecodeNoExport`; obs source forces software for codec MVs (measured) | SATISFIED |
| C6 | Fallback works when side information unavailable | `coarse:"blocksearch"` estimator (labeled `Estimated`, never relabeled) — e2e on the same H.264 clip and by construction for HEVC/AV1 | SATISFIED |

## Mandatory color conditions

| # | Condition | Evidence | Status |
|---|---|---|---|
| K1 | Range/primaries/transfer/matrix/chroma siting tracked when present | `ColorMeta` at decode; obs tests (601/709/range differ, siting honored) | SATISFIED |
| K2 | Working-space conversion explicit | `convertToWorking` declared-space record; unspecified transfer + empty override fails loudly (measured exit 4; `test` configs carry explicit `transferOverride:"srgb"`) | SATISFIED |
| K3 | SDR/HDR do not silently share wrong transfer assumptions | v1 declares display-referred space in every manifest/capture (reconciliation §3.8); overrides recorded | SATISFIED |
| K4 | Metrics/captures know their space | manifest `transferOverride`/declared-space strings; capture sidecars; per-record declared space | SATISFIED |

## Mandatory engineering verification

| # | Condition | Evidence | Status |
|---|---|---|---|
| E1 | Clean configure/build succeeds | clean clone from `git archive HEAD`: configure exit 0, `ninja` exit 0 (312/312 targets) | SATISFIED |
| E2 | Existing applicable tests pass | ctest 29/29 in clean clone (4 GPU tests disabled, 1 asset-skip — pre-existing documented behavior) | SATISFIED |
| E3 | New unit/integration tests pass | `anvil_*` ctest targets + `test_anvil_arms_e2e.py` 13/13 (canonical and clean-clone) | SATISFIED |
| E4 | Vulkan validation clean for exercised new paths | **ADJUDICATED NOT APPLICABLE**: the successor v1 core is CPU-only; `grep -rniE 'vulkan\|vk[A-Z]' src/anvil` → comments only; no new Vulkan path was exercised, so there is nothing to validate. Revisit when a GPU backend lands. | ADJUDICATED N/A |
| E5 | No known resource lifetime or synchronization defect | capture-sink writer-thread join/destructor tests; ring backpressure test; windowOffset re-stamp aliasing documented as a retained risk (no current consumer retains windows) | SATISFIED |
| E6 | Graceful fallback/error reporting tested | exit-2 config errors, exit-3 provenance mismatch, exit-4 runtime/oracle/dimension failures; bad-shape/missing oracle record tests; unspecified-transfer refusal test | SATISFIED |
| E7 | Deterministic replay reproduces same artifacts | `--replay` exit 0 on blocksearch and oracle arms (canonical + arms suite `test_exact_replay_and_tamper`); tamper → exit 3 | SATISFIED |
| E8 | Clean-clone instructions sufficient | `docs/anvil/RUNBOOK.md` verified by executing its path in a fresh `git archive` extraction (build + ctest + arms suite green) | SATISFIED |

## Result

All mandatory conditions are SATISFIED with re-runnable evidence, except E4 which is
explicitly adjudicated not-applicable (CPU-only v1 core; no new Vulkan path exercised).

**`BUILD_READY_FOR_RESEARCH = TRUE`**

Per the execution pack, the broad reconstruction-quality campaign is NOT started; the
recommended experiment order is in [`FINAL_HANDOFF_20261005.md`](FINAL_HANDOFF_20261005.md).
