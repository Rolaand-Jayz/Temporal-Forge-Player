# Temporal Forge Player

> **Closed FSR-era research project · Flagship portfolio work**

**FSR-centered research closed: 2026-09-15.** This repository is preserved as the engineering and evidence record of Temporal Forge's attempt to adapt **AMD FSR 4.1 temporal reconstruction/upscaling to ordinary decoded video**. Future Temporal Forge work is intentionally moving to a custom temporal video-reconstruction architecture rather than continuing to treat FSR as the architectural center.

Read the closure first:

- [`docs/closure/FSR41_FINAL_ADJUDICATION_20260915.md`](docs/closure/FSR41_FINAL_ADJUDICATION_20260915.md)
- [`docs/closure/CLAIM_EVIDENCE_LEDGER.md`](docs/closure/CLAIM_EVIDENCE_LEDGER.md)
- [`docs/closure/LIMITATIONS_AND_OPEN_QUESTIONS.md`](docs/closure/LIMITATIONS_AND_OPEN_QUESTIONS.md)
- [`docs/closure/EVALUATION_STANDARD.md`](docs/closure/EVALUATION_STANDARD.md)

The closure does **not** claim that FSR 4.1 can never work for video. It records a narrower evidence-based decision: the accumulated results no longer justify keeping FSR-specific expected-input reconstruction as Temporal Forge's primary research architecture.

## Historical research target

The player preserves a strict same-cadence contract:

`one decoded video frame → one reconstructed/upscaled displayed frame`

The source frame count, timestamps, and cadence remain the contract. A high-refresh display may repeat a reconstructed frame; the historical player does not invent intermediate frames.

The **FSR 4.1** designation is intentional. This research line targeted the later FSR 4.1 generation through a reverse-engineering-informed integration rather than treating the work as generic FSR4 support.

## Why this was difficult

A native game renderer can provide motion, jitter, reset/history context, exposure, reactive/composition signals, and other semantic inputs designed for temporal reconstruction. Finished video does not naturally preserve those signals in the same form.

Temporal Forge therefore tested whether useful equivalents could be recovered or synthesized from video and whether increasingly plausible temporal inputs produced measurable reconstruction headroom.

The final evidence found that temporal inputs do participate in output, but increasingly sophisticated motion did not produce a repeatable quality gain across the full multi-frame campaign. Strong spatial controls remained competitive or superior on important slices, and composition/pipeline semantics could dominate the result. That evidence drove the architectural pivot rather than an implementation failure.

## Final player state

The preserved player has an operational GPU-native pipeline with:

- **FSR 4.1 RE Experimental** — INT8 reconstruction and the proof-gated temporal path on supported RDNA3 hardware when required runtime/compiler assets are available
- **FSR 3.1.5 (SDK) integration tier** — retained in source but compiled out of the redistributable clean-clone build
- **Spatial fallback** — always-available reliability path after Vulkan initialization when a temporal backend cannot run

Backend failure degrades to spatial scaling with a non-blocking warning instead of silently presenting an unavailable experimental path as successful.

This project does not claim production readiness or parity with AMD's implementation.

## What this project demonstrates

- Native C++23 / Vulkan / FFmpeg / Qt integration on Linux
- GPU video processing and temporal reconstruction
- FSR 4.1 reverse-engineering-informed interoperability research
- reproducible experiment campaigns with binary/source/reference provenance
- explicit separation of measured facts, observations, inferences, hypotheses, and unresolved behavior
- preservation of negative results and invalidated evidence
- causal ablation across motion, jitter, history, recurrent state, scaling, composition, and future-frame probes
- human review capable of reopening a false-green automated quality gate
- independent review, adversarial challenge, remediation loops, and verification gates
- clean-clone portability and explicit licensing/provenance boundaries
- an evidence-based architectural pivot when the inherited solution shape stopped being the strongest research path

The methodology matured during the project. Earlier AMD-first application and reverse-engineering work is part of the lineage; it should not be read as though the final formal process existed from the beginning.

## Core historical rule

```text
1 decoded input frame → 1 reconstructed/upscaled displayed output frame
same timestamps · same frame count · same source frame rate
```

The player reconstructs each source frame at a higher internal resolution through the selected temporal path, then scales that result to the current window or fullscreen surface. Window resizing changes presentation only; it does not redefine the source cadence contract.

## Scaling model

```text
source frame
  → temporal reconstruction target (source × preset ratio)
  → final presentation scale to window
```

| Preset | Ratio |
|---|---:|
| NativeAA | 1.0x |
| Quality | 1.5x |
| Balanced | 1.7x |
| Performance | 2.0x |
| Ultra Performance | 3.0x |

The supersampling campaign found that larger reconstruction grids were conditional rather than universal wins, so reconstruction target and final delivery size remain separate controls in the historical design.

## Clean-clone behavior

The ordinary redistributable build does **not** require a locally installed AMD FidelityFX SDK. The FSR 3.1.5 SDK tier remains source-visible but is not linked into the clean-clone build.

The **FSR 4.1 RE** path has separate runtime/build asset requirements. Native INT8 packs are resolved from portable executable/repository locations, while generic weight blobs can be provisioned through `TFORGE_FSR4_RE_ROOT` or the documented XDG data location. Missing or invalid assets produce diagnostics and fallback rather than a false-success path.

### Git LFS review evidence

Campaign review images under `review_harness/images/*.png` use Git LFS. They are **not required to build, run, or test** the player.

```sh
git lfs install
git lfs pull
```

## Documentation map

Start with [`docs/README.md`](docs/README.md). The key historical/closure entry points are:

- [`docs/current/STATE.md`](docs/current/STATE.md) — current repository status
- [`docs/closure/`](docs/closure/) — final FSR-era adjudication, evaluation standard, claim ledger, and limitations
- [`docs/decisions/TECHNICAL_HISTORY.md`](docs/decisions/TECHNICAL_HISTORY.md) — causal direction changes
- [`docs/FSR4_RE_STATUS.md`](docs/FSR4_RE_STATUS.md) — dated FSR 4.1 RE reconstruction history
- [`benchmarks/quality_sweeps/`](benchmarks/quality_sweeps/) — quality, motion, and causal experiment tooling/evidence
- [`benchmarks/video_corpus/RESULTS.md`](benchmarks/video_corpus/RESULTS.md) — real-world corpus findings
- [`PROVENANCE.md`](PROVENANCE.md) — artifact provenance and unresolved-rights records

Historical plans and progress logs are archived. There is no active FSR campaign in this repository.

## Requirements

The runtime requires a **Vulkan 1.3** driver.

| Package | Role | Required? | Behavior when missing |
|---|---|---|---|
| C++23 compiler (GCC 13+/Clang 17+ class) | build | required | configure/build fails |
| CMake ≥ 3.24 | build | required | configure fails |
| Ninja | build | required | configure fails |
| Qt 6.6+ — Core, Gui, Quick, Qml, Widgets, ShaderTools | build + runtime | required | configure fails |
| glslangValidator | build | required | configure fails |
| Vulkan loader + headers, API 1.3 | build + runtime | required | build fails / runtime cannot start |
| FFmpeg ≥ 5.1 development libraries | build + runtime | required | configure/build fails |
| Python 3 | build tooling | required | tooling steps fail |
| Git LFS | review evidence only | optional | review PNGs remain pointer files; build/runtime unaffected |
| ffmpeg executable with libx264 + aac encoders | test fixtures | optional | affected external-data tests SKIP rather than FAIL |
| jq, ImageMagick | historical research/capture scripts | optional | affected scripts fail with a clear error |
| DXC + SPIR-V tools | native FSR 4.1 pack builds | optional | native pack build tooling cannot run |
| FidelityFX SDK (`TFORGE_ENABLE_FSR1_PROBE=ON`) | optional probe | optional | probe target is not built by default |

Vendored build dependencies include miniaudio v0.11.25 and the repository's Vulkan-header shim; see [`external/README.md`](external/README.md) and [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/temporal_forge_player
```

Set `TFORGE_VK_VALIDATE=1` to enable the Vulkan validation layer.

## Reliability behavior

If a selected experimental backend cannot initialize or execute safely, playback falls back to spatial scaling with a non-blocking warning rather than silently presenting the experimental path as successful.

## License and provenance

Temporal Forge's original code and documentation are licensed under the [Apache License, Version 2.0](LICENSE). Third-party components and reverse-engineering-derived artifacts can have separate licenses or rights status; see [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) and [`PROVENANCE.md`](PROVENANCE.md).

The tracked native INT8 **FSR 4.1 RE** pack data includes reverse-engineering-derived artifacts whose redistribution-rights status is explicitly recorded as **UNRESOLVED / PROVENANCE HOLD**. They are not covered by the project's Apache-2.0 license. This is a provenance record, not a legal conclusion. Generic weight blobs and locally compiled SPIR-V pack modules are not tracked.

## Research status

**Closed FSR-centered research line.** The broader Temporal Forge objective continues outside this architecture: determine how to recover genuine source-supported spatial detail from information distributed across multiple video frames without assuming FSR, optical flow, machine learning, or any fixed temporal architecture in advance.
