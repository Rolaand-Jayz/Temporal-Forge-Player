# ANVIL build goal — final handoff

**Date:** 2026-10-05
**Gate:** [`BUILD_READY_CHECKLIST.md`](BUILD_READY_CHECKLIST.md) — every mandatory condition
satisfied with re-runnable evidence (E4 adjudicated not-applicable: CPU-only v1 core).

**`BUILD_READY_FOR_RESEARCH = TRUE`**

Per the maintainer's goal and the execution pack, the broad reconstruction-quality campaign
is **not started**. This document is the handoff; the maintainer starts that campaign
separately.

## 1. Repository path

`/mnt/workdrive/Temporal-Forge-Player` (clone of `https://github.com/Rolaand-Jayz/Temporal-Forge-Player`)

## 2. Branch and final commit

Branch `anvil/successor-build`; final commit = `git rev-parse HEAD` on that branch (the
commit carrying this document). Historical `main` (at `593b23f0`, the FSR-era closure) is
untouched.

## 3. Tree status

Clean at qualification (verified `git status --short` empty after the final commit).
Regenerable local artifacts (`build-anvil/`, `/mnt/workdrive/anvil_e2e/` example run) are
outside version control.

## 4. Build and test results (all re-runnable)

- Clean clone from `git archive HEAD`: configure exit 0; `ninja` exit 0 (312/312 targets);
  `ctest` **29/29 passed** (4 GPU diagnostic tests disabled, 1 asset-skip — pre-existing,
  documented behavior).
- ANVIL arms end-to-end suite: `python3 -m pytest tests/anvil/test_anvil_arms_e2e.py`
  **13/13 passed** (canonical tree and clean clone; skips cleanly without ffmpeg).
- Deterministic replay: `anvil_runner --replay` exit 0 (byte-identical outputs) on
  blocksearch and oracle arms; tampered manifest → exit 3.
- Loud-failure paths verified: missing oracle record (exit 4), unspecified transfer
  metadata without override (exit 4), malformed config (exit 2).

## 5. Architecture / module map

```
src/anvil/core/      Types.hpp (Observation, SideInfo, MotionField, Visibility,
                     SampleGeometry, ReconstructionResult, ColorMeta), Window.hpp,
                     Sha256 (FIPS 180-4)
src/anvil/obs/       FileObservationSource (wraps src/media in place),
                     convertToWorking (declared working-space boundary)
src/anvil/codecs/    CodecSideInfoClassifier (measured capability table),
                     BlockDensifier (discontinuity-preserving)
src/anvil/stages/    7 stage families + StageFactories:
                     SideInfoNormalizer (passthrough|densify_blocks),
                     CoarseCorrespondence (zero|codec|blocksearch),
                     ResidualRefiner (none|hillclimb2),
                     VisibilityEstimator (unity|photo_fwbw),
                     SampleGeometryEstimator (unknown|global_xform),
                     TemporalReconstructor (center|weighted_blend),
                     OutputBackend (null|passthrough)
src/anvil/oracle/    SidecarOracleSource (anvil.oracle.v1) + oracle twins
                     (OracleCoarse/OracleVisibilityEstimator/OracleGeometryEstimator)
src/anvil/capture/   DiskCaptureSink (bounded ring + writer thread), .npy v1.0 I/O
src/anvil/runner/    anvil_runner CLI (config|replay), PipelineConfig (strict parser,
                     canonicalJson), Manifest (anvil.run_manifest.v1), WindowAssembler
                     (reset events), main (window loop, stage DAG, timings, outputs)
tools/anvil/         make_fixtures.py (deterministic fixture + oracle sidecar + H.264
                     clip), make_vfr_fixture.py
tests/anvil/         anvil_core_tests, anvil_obs_tests, anvil_codecs_tests,
                     anvil_stages_tests, anvil_capture_oracle_tests,
                     anvil_runner_tests, test_anvil_arms_e2e.py
```

CMake: `anvil_core` static lib (explicit FSR-free source set; FFmpeg+Threads only; the
reused `src/media/{Demuxer,VideoDecoder}.cpp` + `src/util/Log.cpp` compile directly into
it — reuse in place, no fork) + `anvil_runner` executable + test targets. `src/anvil` is
excluded from the FSR-era `file(GLOB_RECURSE)` library.

## 6. Old Forge infrastructure reused, and why

- `src/media/` Demuxer + VideoDecoder **in place** (clean FFmpeg-only shell; additive
  extensions only: `pktDtsTicks`, `corrupt`, `setSoftwareDecodeForced`, `setThreadCount` —
  player behavior unchanged, its tests green).
- The backend lifecycle contract **shape** (create/reconfigure/dispatch/resetHistory/
  destroy, timestamp preservation, fail-gracefully) informed the OutputBackend contract.
- Codec-MV prior art: `MvEntry` normalization and the estimator-fallback pattern that
  keeps estimator output distinct from codec MVs.
- Provenance discipline patterns from `benchmarks/quality_sweeps/campaign_provenance.py`
  and `tools/run_baseline_qualification.py` (binary self-hash via `/proc/self/exe`,
  recorded-vs-unrecorded git tri-state, refuse-to-overwrite, empty-metrics-is-failure).
- Kr/Kb matrix tables referenced from `src/render/upload/YuvConstants.cpp`.
- FSR-free tools retained as diff/metric primitives: `compare_stage_equivalence.py`,
  `measure_temporal_sequence.py`, motion-sidecar conventions.

## 7. FSR-era assumptions explicitly NOT inherited

Synthetic camera jitter (Halton/`Jitter.hpp`/SideBufferSynth policy); FSR preset ladder and
fixed scale ratios; fake camera constants and depth/reactive/tcMask shims; `BackendSelector`'s
proof-gated FSR cascade; RE weight blobs / tensor maps / postpass params; the
`TFORGE_FSR4_*` env surface; causal-only one-in/one-out as an architectural constraint
(symmetric offline windows are first-class; causality is an arm); `PlaybackEngine` as a
template; `fract(mean MV)`-style phase inventions. FSR may later appear only as an optional
comparison backend adapter — nothing in `src/anvil/` references it.

## 8. Codec support matrix (measured on this stack: FFmpeg n9.0.2 / libavcodec 63, RDNA3 / Mesa 26.2.4)

| Codec | Codec-MV export (software) | Hardware paths | Successor behavior |
|---|---|---|---|
| H.264 | **YES** (`AV_CODEC_FLAG2_EXPORT_MVS`, P/B frames; quarter-pel; direction ±1 only; bi-pred = two unlinked vectors; no ref identity/temporal distance/skip-intra; I frames emit nothing) | VAAPI/Vulkan: **no export** (measured) | `Coverage::Present` / `IntraFrame`; `coarse:"codec"` arm |
| HEVC | **NO** (measured absent in libavcodec 63.1.102) | same | `Coverage::UnsupportedCodec`; estimator/oracle arms only |
| AV1 | **NO** (measured; libdav1d absent from this build) | same | `Coverage::UnsupportedCodec`; estimator/oracle arms only |
| any hwaccel | — | **no export** (VAAPI + Vulkan measured on RX 7900 GRE) | `Coverage::HwDecodeNoExport`; codec-MV requests force software decode |

Proven limitations are represented explicitly; no field is fabricated; estimator output is
never relabeled as codec MVs; raw side info always survives alongside any densified form.

## 9. Instrumentation / oracle / capture inventory

- `anvil.run_manifest.v1`: runId (replay key = binary/config/input/seed/mode hashes), git
  head+dirty, binary sha256, canonical config + sha256, input + sha256, frameRange
  (emitted/warmup/trailing), window, actual stage names, oracle arms, `resets[]`
  (invalid/discontinuity/seek/dimensions), `transferOverride`, deterministic/seed/decode
  mode, outputs with sha256.
- `timings.json`: per-stage host ms.
- `DiskCaptureSink`: per-(stage, frame) `.npy` + `capture_index.json` (ring + writer thread,
  backpressure, no loss — tested at 200 records / capacity 16).
- Oracle injection: flow, visibility, geometry, HR ground truth via `anvil.oracle.v1`
  sidecars; consumed oracles recorded in the manifest arm list.
- Determinism: software decode with `threads=1`, single-thread CPU core, fixed evaluation
  order, no RNG anywhere in `src/anvil` (grep-verified), seed recorded.
- Reset observability: `WindowAssembler` ResetEvents; stage `onReset()` chain; distinct
  skippedDiscontinuity accounting.

## 10. Deterministic offline-run example

See [`RUNBOOK.md`](RUNBOOK.md). Minimal reproduction:

```
python3 tools/anvil/make_fixtures.py --out /tmp/anvil_fixture
build/anvil_runner --config pipeline.json     # e.g. past=1 future=1, coarse=blocksearch,
                                              # refine=hillclimb2, reconstruct=weighted_blend
build/anvil_runner --replay <outputDir>/run_manifest.json   # -> exit 0, byte-identical
```

A worked example run (14 frames emitted, manifest + capture + timings) is preserved at
`/mnt/workdrive/anvil_e2e/` (outside the repository).

## 11. Known unresolved engineering risks

1. Codec-MV export table is pinned to FFmpeg n9.0.2/libavcodec 63 (measured); a CI/library
   major bump requires re-measurement (reconciliation §5).
2. Float determinism is host-verified (GCC 16.2.1); cross-machine bitwise replay depends on
   compiler/libm behavior — the replay key includes binary hash, and CI reruns the suite.
3. Corrupt-frame and mid-stream dimension-change paths are unit-covered (`WindowAssembler`
   tests) but not end-to-end reproducible with file sources (a fixture cannot hand-craft an
   in-stream invalid observation); the dimension-change exit-4 branch is wired and
   inspected, not e2e-exercised.
4. `Observation.windowOffset` is re-stamped per window emission on shared objects — safe
   for the current consume-immediately pipeline; a future consumer retaining windows would
   need owned copies (documented).
5. `visibility:"photo_fwbw"` uses a luma-based residual (pinned interface carries no
   working RGB at that point — documented deviation); constants 0.08/0.35 are declared
   PROJECT DECISIONs, not tuned.
6. `OracleDataSource::loadGroundTruth` overloads `SourcePlanes` as an RGB carrier; a
   dedicated type is a pre-campaign cleanup candidate.
7. Historical audio decoder defect (missing `swr_convert` resampling) remains unfixed —
   out of the successor's critical path, documented in reconciliation §3.6.

## 12. Hypotheses ready for the later campaign

All eight campaign layers of `LATER_TEST_CAMPAIGN_CONTRACT.md` are now executable arms on
real fixtures and real decode paths:

1. Codec MV blocks are a sufficient coarse-correspondence prior (H.264-only truthfully).
2. One deterministic ±2px SSD hill-climb + parabolic subpixel fit materially refines codec/
   search priors.
3. Photometric residual ∧ forward-backward consistency is a usable backend-neutral
   visibility signal.
4. Sample-geometry state (known/estimated/unknown) can be isolated; UNKNOWN-by-default does
   not lose information that a declared global-transform estimator would recover.
5. Confidence-weighted aligned blending beats single-frame and zero-motion controls when —
   and only when — correspondence and visibility are accurate (crossed oracle arms localize
   which).
6. Future frames add recoverable information over causal-only (future on/off arm).
7. Backend value is separable from temporal-core value (backend swap arm).

## 13. Recommended experiment order for the later campaign

1. **Motion prior truth** on the fixture (oracle vs codec vs blocksearch, crossed with
   estimated/oracle confidence) — validates the harness itself before real video.
2. **Temporal reconstruction truth**: single-frame control vs zero-motion vs aligned blend
   on the fixture; per-neighbor ablation; future on/off.
3. **Uncompressed/lossless real clips** (H.264 lossless or raw) to separate codec-compression
   effects from geometry effects.
4. **Sampling-information truth**: controlled HR→LR observations with exact sample
   positions (the oracle ground-truth plumbing already supports 1:1; extend the fixture
   generator for downsampled observations).
5. **Real compressed video** (H.264 codec arm; HEVC/AV1 via estimator/oracle arms only —
   truthful unsupported states).
6. **Phase estimation** (oracle vs global_xform vs unknown) once 1–5 survive.
7. Performance only after quality/architecture claims survive (per-stage timings exist).

## 14. Gate statement

**`BUILD_READY_FOR_RESEARCH = TRUE`**

The successor system is structurally complete, observable, reproducible, controllable, and
ready for a separate adversarial research campaign. The campaign is not started here.
