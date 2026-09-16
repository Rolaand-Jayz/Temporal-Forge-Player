# Temporal Forge technical history

This is the causal index for major direction changes. It is intentionally short. Dated reports, benchmark evidence, closure documents, and archived plans contain the detailed evidence.

## Initial player architecture

**Problem:** Build a local video player with temporal reconstruction while preserving frame identity and real-time Vulkan operation.

**Decision:** Keep decoding, playback orchestration, Vulkan upload/dispatch, backend selection, and Qt presentation as separate responsibilities. The historical invariants live in [`../reference/ARCHITECTURE.md`](../reference/ARCHITECTURE.md).

## FSR4 reconstruction and proof path

**Problem:** The official FSR4 distribution did not provide the needed native Linux/Vulkan path.

**Investigation:** The project reconstructed the model and dispatch contract from available evidence, then added typed parameter, graph, and runtime proof checks.

**Decision:** Treat the FSR4 INT8 path as experimental and proof-gated. Failure cascades to the SDK tier when compiled/available or to spatial fallback. The dated reconstruction record is [`../reports/FSR4_RECONSTRUCTION_STATUS_20260709.md`](../reports/FSR4_RECONSTRUCTION_STATUS_20260709.md).

## Temporal contracts and motion

**Problem:** Video does not provide the same motion, jitter, reset, and side inputs as a rendered game frame.

**Evidence:** M0 through M5 contracts formalized provenance, postpass parameters, reprojection state, causal motion, color metadata, and jitter. Later motion campaigns compared zero, codec, cheap/refined, edge-aware, and offline dense motion across a full multi-frame matrix.

**Decision at the time:** Keep those inputs explicit and evidence-bound. Codec/refined motion and synthetic jitter remained separate experimental controls, not assumed truth.

**Later result:** The completed motion matrix did not show repeatable reconstruction headroom from increasingly sophisticated motion. Zero motion was best or near-best across most source tiers, while better-flow arms often increased temporal error. This became a major input to the final FSR-era closure.

## Supersampling and delivery scale

**Question:** Does increasing the FSR reconstruction grid improve output after delivery reduction?

**Evidence:** The 2026-08-31 supersampling report found aggregate gains for some real scenes, but losses on other scene slices and higher memory cost.

**Decision:** Retain independent reconstruction and delivery dimensions as controls. Do not make 3x a universal default.

## Capture and provenance recovery

**Problem:** Earlier campaign and harness outputs did not establish complete, truthful coverage for every required method and resolution.

**Decision:** Re-capture required evidence with explicit method, resolution, timestamp, binary, configuration, and reference provenance. Preserve old reports as historical rather than rewriting them as though produced by the recovery run.

## Human-review reopen of lattice qualification

**Problem:** An automated qualification reported success while human review identified visible periodic lattice corruption.

**Decision:** Reopen the P0, explicitly invalidate the prior canonical campaign as a baseline, preserve both the automated PASS and human-review FAIL, and trace the defect through matched ablation rather than lowering the review standard.

**Consequence:** Human review became an explicit reopen mechanism for concrete artifact classes that scalar metrics failed to detect.

## Clean-clone portability and provenance remediation

**Problem:** Public documentation, runtime paths, build dependencies, and local asset assumptions could make repository claims dependent on the maintainer's machine.

**Decision:** Run a clean-clone portability/remediation campaign, broaden the audit beyond the initial findings, require three consecutive remediation-free loops, and require independent verification. Licensing and reverse-engineering provenance boundaries were recorded explicitly.

**Result:** The campaign qualified on 2026-09-09 and later merged to `main`.

## Final Expected-Food checkpoint

**Purpose:** Establish a clean pre-campaign state for the last FSR expected-input research sprint.

**Repository result:** The final development checkpoint was merged to `main` on 2026-09-12 at `285a5788f89787bce0ca26f8e8e8ca312890723f` (`Merge canonical quality-lab checkpoint for Expected-Food sprint`).

**Interpretation:** The checkpoint does not supersede the accumulated negative/conditional evidence and is not treated as proof that a missing expected-input mapping was solved.

## 2026-09-15 — close FSR as the architectural center

**Question:** After the correctness, motion, jitter, history, supersampling, composition, causal diagnostics, portability, and Expected-Food work, is continued FSR-specific adaptation still the strongest path to Temporal Forge's actual objective?

**Evidence:**

- temporal inputs measurably participate in output, but increasingly plausible motion did not produce repeatable quality gains;
- source-detail loss dominated the motion-estimator ordering in the full campaign;
- history, jitter, recurrence, future-frame midpoint, block-motion, and dense-correspondence probes did not reveal a universal missing-input fix;
- strong spatial controls remained competitive or superior on important slices;
- larger reconstruction grids were conditional rather than universal wins;
- composition, color/history semantics, geometry, and resolve behavior could dominate quality independently of the presumed missing input;
- the evidence system repeatedly rejected or reopened attractive but insufficient results rather than promoting them.

**Decision:** Close the FSR-centered era. Preserve the player and research corpus as a historical engineering/evidence record, but stop treating FSR 4.1's game-renderer contract as the default architecture for future Temporal Forge work.

**Nonclaim:** This does not prove FSR 4.1 can never be adapted successfully to video.

**Successor rule:** Begin from the broader reconstruction objective — recovering genuine source-supported detail from temporally distributed video observations — and independently choose alignment, temporal support, deterministic/learned stages, real-time/offline constraints, and reconstruction architecture.

The authoritative closure record is [`../closure/FSR41_FINAL_ADJUDICATION_20260915.md`](../closure/FSR41_FINAL_ADJUDICATION_20260915.md).

## Current uncertainty

The FSR-centered architecture is closed; the broader temporal-reconstruction problem is not. Open questions are recorded in [`../closure/LIMITATIONS_AND_OPEN_QUESTIONS.md`](../closure/LIMITATIONS_AND_OPEN_QUESTIONS.md). The successor architecture is intentionally not selected in this repository.
