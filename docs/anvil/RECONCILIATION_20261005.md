# ANVIL successor reconciliation — audit wave results and engineering decisions

**Date:** 2026-10-05
**Branch:** `anvil/successor-build` (base `main` @ `593b23f0`)
**Input:** 12-stream parallel audit wave (GLM 5.3 Flash workers, run `dwfrun-015b2ece`) over
`/mnt/workdrive/Temporal-Forge-Player` and `/mnt/workdrive/RE-of-FSR-4.1.0-Upscaling`; full
reports in the run's published audit-wave artifact. Evidence classes follow
[`../closure/EVALUATION_STANDARD.md`](../closure/EVALUATION_STANDARD.md).

This document is the Phase-1 exit required by the orchestration plan: one reconciled
successor engineering decision, with conflicts kept explicit. No large implementation
began before it.

## 1. Verified baseline (MEASURED FACT, this host)

The audit wave's build/CI worker executed a clean configure + build + test on this machine:

- `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` → exit 0 (GCC 16.2.1, FFmpeg 63.1.102 /
  61.1.102, Vulkan 1.4.356, Qt6 6.11.2, glslang 16.6.0).
- `ninja` → exit 0, 258/258 targets, only `-Wpedantic __int128` warnings from
  `src/util/Jitter.hpp:70`.
- `ctest` → 23/27 passed, 1 skip (fsr4_tensormap_tests, exit-77 asset skip), 4 GPU tests
  disabled by default — matches `CONTRIBUTING.md:15`.

No submodules; system deps only; no install rules; CI is Arch-container build+ctest plus a
pytest job over 6 contract files. The successor inherits this as its starting point.

## 2. Environment truth (MEASURED FACT)

- AMD RX 7900 GRE (RADV NAVI31), Mesa 26.2.4, Vulkan 1.4.354; VAAPI 1.24.
- FFmpeg n9.0.2 (headers: `frame.h`, not `avframe.h`; `AV_FRAME_DATA_MOTION_VECTORS` = 8).
- Codec motion-vector export measured on this stack:
  - **H.264 software decode: WORKS** (`AV_CODEC_FLAG2_EXPORT_MVS` + side data on P/B frames).
  - **HEVC software decode: DOES NOT EXPORT** (measured, libavcodec 63.1.102).
  - **AV1 software decode: DOES NOT EXPORT** (measured; libdav1d absent from this build).
  - **VAAPI and Vulkan hardware decode: DO NOT EXPORT** (both measured on Navi31/Mesa 26.2.4).
  - `AVMotionVector` semantics measured: quarter-pel (`motion_scale=4`), `src` integer-truncated
    with fraction only in `motion_x/y`, bi-pred appears as two unlinked vectors (`source=±1`),
    no reference identity / temporal distance / per-vector frame type / skip-intra fields,
    I-frames emit no side data at all (so "empty" is ambiguous).
- FSR RE claim ceiling (from RE repo `VALIDATION_STATUS.md`): runtime pass order and runtime
  CBV values are RUNTIME-NOT-OBSERVED (confidence 0.00); only weight-blob identity, entrypoint
  inventory, ReLU activation, postpass-scatter, blob format are STATIC-REPRODUCIBLE.

## 3. Architecture decisions (PROJECT DECISION unless labeled otherwise)

### 3.1 Where the successor lives

A new module tree `src/anvil/` plus a new CMake executable target `anvil_runner` and a static
library `anvil_core` that compiles an **explicit** FSR-free source list. The existing
`temporal_forge_lib` is a `file(GLOB_RECURSE)` over all `src/*.cpp` with PUBLIC dependencies on
generated FSR shader headers (`CMakeLists.txt:152-181`); linking it would drag the whole FSR
build graph into the successor. The old player binary keeps building unchanged.

Layout (acyclic DAG; `core` is a leaf):

```
src/anvil/core/       types.hpp (Observation, MotionField, Visibility, SampleGeometry,
                      ReconstructionResult, ColorMeta), window.hpp, determinism.hpp
src/anvil/obs/        observation source (wraps src/media decode), planar plane views
src/anvil/codecs/     codec side-information adapters (measured capability table)
src/anvil/stages/     sideinfo normalize, coarse, refine, visibility, geometry, reconstruct
src/anvil/oracle/     OracleSource implementations per boundary, oracle flow/sidecar readers
src/anvil/capture/    StageCaptureSink (ring -> disk), naming, formats
src/anvil/runner/     offline headless runner, pipeline config (JSON), run manifest writer
src/anvil/backends/   OutputBackend: null + passthrough v1; Vulkan upscale adapter later
tests/anvil/          successor tests via the existing tforge_add_test pattern
```

### 3.2 What is reused and why (evidence-based)

- `src/media/` Demuxer + VideoDecoder — clean FFmpeg-only shell, no Vulkan/FSR includes
  (topology + decode audits). Reused in place; anvil adds an observation-adapter layer on
  top rather than forking, with two permitted extensions on the successor branch (see 3.6).
- `src/render/VulkanContext` — reusable standalone (owned-instance path, pure device-request
  planner). v1 successor core is CPU-deterministic; Vulkan is used only by the optional GPU
  output backend later. For headless use the fatal `VK_KHR_SWAPCHAIN` device-extension gate
  (`VulkanContext.cpp:55-58`) and unconditional surface instance extensions (`:255-263`)
  must become headless-mode-conditional (small, non-behavioral-for-player change).
- Backend lifecycle contract shape (`ITemporalUpscalerBackend`): 6-method lifecycle,
  1:1 frame invariant, `resetHistory()`, timestamp preservation, fail-gracefully. The
  **contract shape** is reused; the packet type (`VideoFsrPacket`) is not — it is FSR-shaped
  (jitter fields, fake depth/reactive shims, FSR preset ratios in shared types).
- Provenance discipline patterns: binary self-hashing via `/proc/self/exe`, recorded-vs-unrecorded
  git tri-state with on-disk re-verification, refuse-to-overwrite outputs, empty-metrics-is-failure
  (`benchmarks/quality_sweeps/campaign_provenance.py`, `tools/run_baseline_qualification.py`).
- FSR-free tools: `tools/compare_stage_equivalence.py` (numeric stage diff gate with format
  tolerances), `tools/measure_temporal_sequence.py` (temporal metrics), motion sidecar
  schema `temporal_forge.codec_motion.v1` conventions.
- Prior art for codec MVs: `tools/inspect_ffmpeg_mvs.cpp`, `src/media/VideoDecoder.cpp`
  `MvEntry` normalization, and the estimator-fallback pattern that keeps estimator output
  mode-distinct from codec MVs (`PlaybackEngine.cpp:3156-3172`).
- `TemporalFrameContinuity` concept (frame-index reset detection) — reimplemented in anvil
  core with window semantics, not linked from the engine.

### 3.3 What is explicitly NOT inherited (FSR-era assumptions)

Per the closure adjudication's successor-independence rule and the fsr-isolation audit:

1. Synthetic camera jitter (Halton sequences, `src/util/Jitter.hpp`, `SideBufferSynth`
   jitter policy) — prerecorded video has no camera jitter; the successor's
   SampleGeometry is UNKNOWN-by-default.
2. FSR preset ladder / fixed scale ratios / `FsrTargetMath` sizing.
3. Fake camera constants and depth/reactive/tcMask compatibility shims.
4. `BackendSelector`'s FSR proof-gated cascade as core policy.
5. FSR4 RE blobs, tensor maps, postpass params, `WeightBlob`, any `TFORGE_FSR4_*` env surface.
6. Causal-only one-frame-in/one-frame-out as an architectural constraint (offline symmetric
   windows are first-class; causal vs acausal is an experiment arm, not an assumption).
7. `PlaybackEngine` as a template (5036-LoC monolith, FSR dispatch inlined in the decode
   loop, audio-clock pacing, Qt coupling).
8. `fract(mean MV)`-style phase inventions; sample phase is KNOWN (oracle), ESTIMATED
   (declared estimator), or UNKNOWN — never fabricated.

### 3.4 Core data model (v1)

```cpp
struct ColorMeta {  // resolved decode-side metadata; "unspecified" is preserved, not baked
  enum Range { Unspecified, Limited, Full };
  int matrix, primaries, transfer;  // AVColor* values
  Range range; int chromaLocation; int bitDepth;
};
struct SideInfo {
  enum class Provenance { None, CodecMv, External, Oracle };
  enum class Coverage { Present, IntraFrame, UnsupportedCodec, HwDecodeNoExport };
  Provenance provenance; Coverage coverage;   // empty vector list must not be ambiguous
  std::vector<AnvilMv> mvs;                   // dst block, w/h, mv px, direction, quality
  bool bFrame;                                // no ref-identity claim is made from this
};
struct Observation {
  uint64_t fileFrameIndex;    // stable across seeks: (sourceId, seekGeneration, decoderIndex)
  int64_t ptsUs, dtsUs;       // dts recorded when present
  Rational timebase; bool valid; // decode_error_flags / concealed -> valid=false
  Planes planes;              // owned host planes, avPixelFormat
  ColorMeta color; SideInfo side;
  enum class Rel { Past, Center, Future } rel;
};
struct MotionField { enum class Src { Oracle, Codec, Estimated } src;
  std::vector<Vec2f> fwd, bwd; std::vector<float> fwbwError; };
struct Visibility { enum class Src { Oracle, Estimated } src;
  std::vector<float> weight; std::vector<uint8_t> occluded; };
struct SampleGeometry { enum class Status { Known, Estimated, Unknown } status = Unknown;
  Mat3f globalXform; };       // no jitter synthesis, ever
struct ReconstructionResult { uint64_t fileFrameIndex; Planes out; SampleGeometry geo;
  AblationRecord ablation; };
```

### 3.5 Stage interfaces and pipeline

Seven stage boundaries, each an abstract interface with a **null/bypass implementation** and
an **oracle twin selected by config**, composed only by the runner (no stage calls another
stage's interface): `SideInfoNormalizer`, `CoarseCorrespondence`, `ResidualRefiner`,
`VisibilityEstimator`, `SampleGeometryEstimator`, `TemporalReconstructor`, `OutputBackend`.

v1 minimal algorithms (HYPOTHESIS class — simple, deterministic, testable, not quality bets):

- Coarse: codec MV blocks (H.264) when present; otherwise deterministic 8×8 block search
  on luma, labeled `Estimated` — never relabeled as codec MVs.
- Refine: one deterministic ±2px SSD hill-climb in fixed scan order + 1-D parabolic subpixel
  fit; adopt only on improvement > ε (tuning precedent `MotionEstimator.hpp:19-31`).
- Visibility: photometric residual ∧ forward-backward consistency (comparison at the warped
  position, per guardrails).
- Geometry: UNKNOWN default; optional declared global-transform estimator; KNOWN only from oracle.
- Reconstruct: confidence-weighted aligned blend with single-frame control (center-only
  weights) and per-neighbor ablation; no artificial resampling counted as new information.

### 3.6 Permitted extensions to reused historical code

The successor branch may extend `src/media/` and `src/render/VulkanContext` only where the
successor contract requires it, keeping the old player's behavior and tests green:

- `VideoDecoder`: expose DTS, decode error/validity flags, generic `AVFrameSideData`
  enumeration, and a seek-generation-stable frame identity. Additive; no behavior change for
  the player path.
- `VulkanContext`: make the swapchain/surface extension gates conditional on a headless init
  flag. Additive; default player path unchanged.
- Fix the audio decoder's missing `swr_convert` resampling (a measured defect:
  `AudioDecoder.cpp` configures `swr_` but never calls `swr_convert`, breaking timestamps
  when input rate ≠ output rate) — only if/when anvil consumes audio; not on the build-ready
  critical path.

### 3.7 Observability substrate (from the observability + harness audits)

- **Run manifest** `anvil.run_manifest.v1`: schema, runId, createdAt, git{head,dirty},
  binary{path,sha256}, config{canonicalJson,sha256}, inputs[{path,sha256,frameRange}], seed,
  determinismMode(exact|best_effort), decodeMode(software|hwaccel), stage selections,
  arm list, environment{gpu,driver,os} (informational). **Replay key** = (binary sha, config
  sha, input sha, seed, determinism mode, decode mode).
- **Stage capture** `StageCaptureSink`: per (stageId, fileFrameIndex, ordinal) records to
  `.npy` (numeric exact), `.png` (8-bit proxies), `.raw`+`.json` sidecar (dtype/shape/scale);
  each record carries the stage's input hashes; writer thread drains a bounded ring.
- **Oracle injection** at every boundary; every consumed oracle is recorded in the manifest
  arm list. Synthetic fixtures are build-time generated (ffmpeg lavfi, deterministic) with
  HR ground truth attached.
- **Timing** `timings.json` per run: stageId → hostMs[] (+ deviceMs[] where GPU).
- **Deterministic mode**: software decode with fixed thread count, single-thread CPU core,
  no atomics in the deterministic path, fixed float evaluation order, no RNG (none exists),
  recorded seed for any future stochastic component. Decode must default to software in
  deterministic mode (hwaccel default is a measured non-determinism source).
- **Replay**: `anvil_runner --replay <manifest>` re-derives config, asserts hashes, runs
  exact mode, diffs stage artifacts via the equivalence tool.
- Test arms are config, not forks: crossed oracle/estimated flow+confidence arms,
  single-frame control, per-neighbor ablation, codec prior on/off, refinement on/off,
  future on/off, backend comparison, exact replay.

### 3.8 Color handling

`ColorMeta` resolved at decode with explicit "unspecified" preservation. One explicit
working-space boundary: decode → normalized planar float working space (range-explicit,
inverse-EOTF selected from recorded transfer — v1 supports declared sRGB/709/linear/PQ/HLG
mapping but REFUSES unspecified-transfer content in deterministic mode rather than assuming),
matrix conversion from recorded matrix, chroma upsampling at recorded siting. Metrics and
captures record which space they operate in. The old pipeline's silent assumptions
(sRGB-EOTF default, primaries dropped, sws normalization without colorspace details) are
documented as FSR-era behavior and not inherited.

### 3.9 Codec side-information contract (measured, per guardrails)

Capability is a measured table, not an assumption: `{h264: sw export yes; hevc: no; av1: no;
any hwaccel: no}`. Adapter outputs carry provenance (`CodecMv`/`External`/`Oracle`/`None`)
and coverage (`Present`/`IntraFrame`/`UnsupportedCodec`/`HwDecodeNoExport`) so "no vectors"
is never silently interpreted. No field is fabricated (no invented reference identity, no
invented skip/intra). Estimator output never enters a field labeled codec MVs. Raw side
information is preserved alongside any densified form. HEVC/AV1 remain honestly unsupported
for codec-MV priors on this stack; the generic path (block-search estimation, oracle flow)
does not depend on them.

## 4. Conflicts and their resolution (kept explicit)

1. **Hook carrier**: observability audit proposed riding `ITemporalUpscalerBackend`;
   proposal audit proposed new stage interfaces. **Resolution:** new anvil stage interfaces
   (the old interface's packet is FSR-shaped); the old 6-method lifecycle *shape* informs
   the OutputBackend contract only.
2. **GPU vs CPU for v1**: proposal audit says CPU-only deterministic core; Vulkan audit
   shows mature GPU infra exists. **Resolution:** CPU-deterministic v1 core (bitwise
   reproducibility is the build-ready requirement; GPU compute variance is a later,
   explicitly-labeled option behind the same interfaces). The GPU output backend is
   permitted but not required at the gate.
3. **Reuse `src/media` in place vs fork**: topology audit suggested explicit-file-list
   target (reuse in place); decode audit found player-fused state. **Resolution:** reuse
   Demuxer/VideoDecoder in place with additive extensions (3.6); the anvil runner never
   links `PlaybackEngine`.
4. **`TFORGE_HEADLESS_BENCHMARK`** exists but still requires Qt/QML (measured). **Resolution:**
   it is not a headless runner; anvil gets its own Qt-free `main`.

## 5. Open questions carried into the build (UNRESOLVED by design)

- Whether `AV_FRAME_DATA_MOTION_VECTORS` for H.264 remains stable across FFmpeg major
  versions used in CI (currently measured on n9.0.2 only).
- Exact VFR windowing policy (frame-count vs PTS-bounded window) — decided during Phase 2
  implementation with fixture evidence.
- Whether `.npy` writers need a new vendored dependency or a minimal hand-rolled writer —
  decided in Phase 2 (preference: hand-rolled, no new deps).

## 6. Phase mapping to the BUILD_READY_FOR_RESEARCH contract

| Contract area | Built in | Verified by |
|---|---|---|
| Successor path isolated, FSR-optional | Phase 2 (new targets) | clean build without FSR flags |
| Observation model, window, stages | Phase 2/3 | unit + integration tests |
| Headless deterministic runner + manifests + capture + oracles | Phase 2/3 | replay test, manifest test |
| Codec conditions (H.264/HEVC/AV1 matrix) | Phase 3 | measured-capability tests + fixture tests |
| Color conditions | Phase 3 | color metadata contract tests |
| Deterministic replay, arms auditable | Phase 3/4 | replay + ablation tests |
| Clean build/CI | Phase 4 | ctest suite + CI extension |

Build order continues per the orchestration plan: Phase 2 substrate → Phase 3 minimal
complete candidate → Phase 4 hardening → Phase 5 qualification → Phase 6 stop before the
research campaign.
