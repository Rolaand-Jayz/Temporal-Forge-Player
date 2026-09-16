# Temporal Forge FSR 4.1 final adjudication

**Date:** 2026-09-15  
**Status:** **CLOSED — FSR 4.1 adaptation is no longer the architectural center of Temporal Forge**  
**Evidence standard:** [`EVALUATION_STANDARD.md`](EVALUATION_STANDARD.md)

## Decision

Temporal Forge closes its FSR-centered research era.

The repository succeeded at building a credible, proof-gated Linux/Vulkan experimental path for applying reverse-engineering-informed AMD FSR 4.1 temporal reconstruction to ordinary decoded video. It also built a stronger result around that implementation: a reproducible evidence system capable of falsifying attractive hypotheses, preserving negative results, reopening false-green gates, and distinguishing engineering success from reconstruction-quality success.

The cumulative evidence does **not** justify continuing to make FSR 4.1's expected renderer inputs, graph behavior, or integration contract the architectural center of future Temporal Forge work.

This is a project decision supported by the evidence below. It is **not** a claim that FSR 4.1 can never be useful for video.

## Original research proposition

The FSR-centered approach asked whether a high-quality game temporal reconstructor could be adapted to finished video by recovering, estimating, synthesizing, or approximating the information a renderer normally supplies — especially motion, jitter, history/reset context, exposure/color semantics, confidence/masks, and composition-related inputs.

The strongest version of that proposition predicted that increasingly correct surrogate inputs should expose meaningful reconstruction headroom.

That prediction was not supported strongly enough to remain the project's primary architecture.

## What the FSR era established

### 1. The experimental path was real

The project reconstructed and integrated enough of the FSR 4/4.1 contract to exercise a native INT8 experimental path on the target RDNA3/Vulkan system. Runtime proof gates, backend fallback, asset resolution, source/binary identity, and failure diagnostics distinguish successful dispatch from false success.

The engineering result therefore cannot be dismissed as a mocked or purely conceptual FSR path.

### 2. Temporal semantics matter, but the tested surrogates do not map cleanly to quality

Matched experiments showed that motion, history, jitter, recurrent behavior, and composition can change the output. They are not inert.

However, changing a temporal input is not the same as demonstrating that a more plausible version of that input improves reconstruction.

### 3. Better motion failed the strongest expected-input test

The completed motion campaign is the most direct evidence against continuing the FSR-input-reconstruction strategy as the primary path.

The multi-frame matrix contains 288 keys across four source tiers, four scenes, six motion arms, and three confidence settings, with eight-frame captures, dense motion/validity artifacts, traces, metrics, and offline-flow validation.

Its result is consistent across the matrix:

- zero motion is best or near-best at three source tiers;
- increasingly sophisticated motion arms do not produce repeatable SSIM gains;
- offline dense flow does not establish a quality upper bound;
- the offline arm frequently increases temporal-delta error;
- confidence-threshold changes are small relative to the dominant quality differences;
- lower source resolution causes a much larger quality collapse than estimator sophistication repairs.

See [`../../benchmarks/quality_sweeps/MOTION_CAMPAIGN.md`](../../benchmarks/quality_sweeps/MOTION_CAMPAIGN.md).

This does not prove motion is irrelevant. It shows that, within the tested FSR path, **better correspondence did not translate into the expected reconstruction benefit**.

### 4. History, jitter, recurrence, and interpolation probes did not reveal a missing silver bullet

The real-world corpus contains targeted motion, jitter, history, recurrent-state, midpoint, future-frame, block-motion, and dense-correspondence probes.

Important outcomes include:

- rooftop motion changes the result but does not close the temporal deficit to Lanczos;
- disabling jitter is not a general fix;
- disabling history can make the temporal result worse rather than repairing it;
- corrected recurrent feedback can improve a malformed case but does not close the control gap;
- raw midpoint synthesis is not a valid substitute for a correct temporal sample;
- cheap motion-compensated midpoint synthesis remains below matched controls;
- validated dense correspondence improves the cheap midpoint path only slightly and still does not recover the missing temporal benefit.

See [`../../benchmarks/video_corpus/RESULTS.md`](../../benchmarks/video_corpus/RESULTS.md).

These experiments narrow the space. They do not establish that a purpose-built multi-frame reconstructor will fail; they establish that forcing the problem through the tested FSR-side semantics is not yielding proportional quality returns.

### 5. Supersampling helped conditionally, not universally

Increasing the reconstruction grid produced gains on some slices and regressions on others while increasing memory cost. That evidence supported keeping reconstruction size and delivery size separate, not adopting a universal larger-grid policy.

See [`../reports/FSR4_SUPERSAMPLING_REPORT_20260831.md`](../reports/FSR4_SUPERSAMPLING_REPORT_20260831.md).

### 6. Strong spatial controls remained competitive or superior in important slices

M6 and corpus work repeatedly showed that the learned/temporal path was not a universal quality winner. Some slices improved; others lost spatial quality, temporal stability, or both to strong spatial controls.

This matters because the research objective is not to make FSR execute. It is to recover better video.

### 7. Composition and pipeline semantics sometimes mattered more than the presumed missing input

Quality Lab work showed that postpass composition, learned strength, color/history handling, scaling geometry, and resolve semantics can dominate the measured image. The lattice investigation additionally showed that subtle pipeline semantics can overwhelm nominal model quality.

That shifts the problem away from a simple "find the missing motion/jitter input" framing.

### 8. The evidence process itself survived adversarial correction

A prior automated quality qualification was reopened after human review identified a visible periodic lattice. The affected campaign was explicitly invalidated as a baseline, the defect was traced through controlled ablation, and subsequent evidence retained the failed history rather than rewriting it.

See [`../LATTICE_CORRUPTION_DIAGNOSTIC.md`](../LATTICE_CORRUPTION_DIAGNOSTIC.md) and [`../reports/QUALITY_LAB_REVIEW_ADJUDICATION_20260906.md`](../reports/QUALITY_LAB_REVIEW_ADJUDICATION_20260906.md).

This is material to the final decision: the closure rests on a process that demonstrated willingness to overturn its own apparent success.

### 9. The repository itself was made reproducible enough to preserve the result

The portability/remediation campaign removed maintainer-specific assumptions, corrected dependency/runtime documentation, hardened clean-clone behavior, completed three consecutive remediation-free loops, and received independent Luna verification. Licensing/provenance boundaries were made explicit.

The result is a research record that can be inspected independently rather than a conclusion dependent on one workstation.

## Expected-Food checkpoint

The repository was cleaned and checkpointed for the final Expected-Food sprint, and `main` ends that development line at commit `285a5788f89787bce0ca26f8e8e8ca312890723f` (`Merge canonical quality-lab checkpoint for Expected-Food sprint`).

That checkpoint does not overturn the cumulative evidence above and is not treated here as proof that an untested expected-input mapping succeeded. The closure therefore does not manufacture a stronger Expected-Food result than the repository actually contains.

The evidence-backed conclusion is narrower and sufficient: continuing to chase FSR-specific expected inputs is no longer the strongest research allocation for Temporal Forge.

## Why the architecture is being retired rather than merely paused

The project objective is broader than FSR:

> recover as much genuine source-supported spatial detail as possible from low-resolution, blurry, compressed, or bandwidth-constrained video by exploiting information distributed across multiple frames.

FSR was one candidate mechanism for that objective.

By closure, the project had spent substantial effort making the FSR path more semantically correct, more observable, more reproducible, and better supplied with motion/history/jitter evidence. The expected quality headroom did not emerge proportionally. Continuing would increasingly optimize an inherited game-upscaler contract instead of asking what architecture best matches video itself.

The correct next experiment is therefore architectural freedom.

## Final dispositions

- **FSR 4.1 Linux/Vulkan experimental integration:** retained as a successful engineering/research artifact.
- **FSR-specific expected-input reconstruction as the main Temporal Forge strategy:** closed.
- **Current player and benchmark infrastructure:** preserved as historical evidence and a source of reusable techniques, not as mandatory successor architecture.
- **FSR-specific motion/jitter/history assumptions:** not inherited by default.
- **Temporal multi-frame reconstruction objective:** continues.
- **Successor architecture:** intentionally unresolved at this closure point.

See [`CLAIM_EVIDENCE_LEDGER.md`](CLAIM_EVIDENCE_LEDGER.md) and [`LIMITATIONS_AND_OPEN_QUESTIONS.md`](LIMITATIONS_AND_OPEN_QUESTIONS.md).

## Successor independence rule

A successor Temporal Forge implementation must begin from the reconstruction problem rather than from this repository's solution shape.

It may reuse algorithms, code, metrics, datasets, benchmark methods, temporal insights, or negative evidence from this repository only when independently justified for the new architecture.

It must not assume, merely because this repository did:

- FSR-style jitter is required;
- game-style motion vectors are the correct alignment representation;
- a causal recurrent history is the correct temporal memory;
- a fixed reconstruction ratio is the correct scale model;
- one input frame must map to exactly one output frame if an offline mode has different objectives;
- ML is required for every stage;
- optical flow is required;
- the temporal support window should be fixed;
- the FSR postpass/composition structure should survive.

The old repository is a source of evidence, not a template.

## Closure statement

The FSR era of Temporal Forge is complete.

It produced working software, reverse-engineering-informed integration, a large body of controlled evidence, several disproven intuitions, improved research discipline, and a clearer statement of the actual problem.

That is enough.

Future work should spend its uncertainty budget on the video reconstruction problem itself.
