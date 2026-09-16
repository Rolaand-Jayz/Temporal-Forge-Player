# Temporal Forge FSR-era limitations and open questions

**Closure date:** 2026-09-15

This document defines what the closed FSR-centered repository does **not** establish. It is part of the evidence standard, not a disclaimer added after the fact.

## Demonstrated boundaries

- The project demonstrates an operational Linux/Vulkan/RDNA3 research path that adapts reverse-engineering-informed FSR 4.1 reconstruction to decoded video under explicit proof/fallback gates.
- It demonstrates substantial experiment infrastructure, provenance, causal ablation, temporal metrics, human review, portability remediation, and negative-result preservation.
- It does **not** demonstrate production readiness, universal quality superiority, parity with AMD's internal implementation, or hardware-vendor portability of the FSR 4.1 experimental path.

## Evidence limitations

### Scene and corpus coverage

The repository contains several real-world clips and controlled campaign scenes, but it is not an exhaustive distribution of film, animation, games, broadcast video, low-light footage, camera noise, extreme compression, interlaced sources, variable frame rate, HDR formats, or arbitrary codec histories.

A conditional result on the retained corpus must not be generalized to all video.

### Metric limitations

SSIM and temporal-delta metrics capture useful error dimensions but do not reliably detect every perceptual artifact. The visible-lattice incident demonstrated that an automated gate can pass while a human reviewer still identifies a real periodic defect.

No single scalar metric is treated as a complete perceptual-quality oracle.

### Human-review limitations

Human visual review is concrete evidence of visible defects or improvements in the reviewed material, but it is not blinded psychophysics, a population study, or a substitute for reproducible captures and controls.

### Hardware scope

The core experimental path was developed primarily around AMD RDNA3 hardware and the local Linux/Vulkan stack. Clean-clone portability addresses hidden repository/environment coupling; it does not convert the experimental backend into a vendor-agnostic implementation.

### Reverse-engineering scope

The project reconstructed enough FSR 4/4.1 behavior and assets to support its experimental path, but it does not claim complete knowledge of AMD's private training process, hidden implementation details, internal quality heuristics, proprietary tooling, or all renderer-side semantics expected by production integrations.

### Rights/provenance scope

Reverse-engineering-derived native INT8 pack data remains under the explicit **UNRESOLVED / PROVENANCE HOLD** described in [`../../PROVENANCE.md`](../../PROVENANCE.md). The project license does not erase third-party or unresolved rights status.

### Performance qualification

Not every quality capture is performance-qualified. The repository explicitly distinguishes image-quality evidence from runs with inadequate GPU timing or uncertain contention provenance. Gaming or other user activity was never stopped for capture and can disqualify a run as clean performance evidence without invalidating its image-quality payload.

## Invalidated or superseded evidence

Evidence can remain historically valuable while being unfit for promotion.

Notable examples include:

- the canonical quality campaign state that was invalidated after human review found visible periodic lattice corruption despite an automated PASS;
- captures whose hardware-surface path prevented the intended interpolation synthesis;
- comparisons with reference timing mismatched to synthesized midpoint timestamps;
- earlier campaign roots whose method/resolution/provenance coverage could not satisfy the superseding campaign contract;
- binaries or local captures whose exact source identity was not sufficiently established for clean promotion.

These are preserved as causal history where useful but must not be cited as valid support for stronger claims.

## Open technical questions intentionally carried forward

The FSR-era closure does not resolve the following broader Temporal Forge questions:

1. How much genuine high-frequency spatial information can be recovered from multiple ordinary video frames when observations are accurately aligned and visibility is modeled?
2. Which alignment representation is strongest for reconstruction: dense flow, deformable sampling, feature matching, block motion, learned correspondence, frequency-domain alignment, implicit alignment, or hybrids?
3. How should occlusion, disocclusion, non-rigid motion, transparency, particles, motion blur, rolling shutter, exposure variation, compression artifacts, and lighting change be modeled?
4. How much can future frames improve offline reconstruction relative to causal-only real-time reconstruction?
5. What temporal support should be adaptive rather than a fixed frame window?
6. Which stages benefit from deterministic signal processing, which from learned models, and where do hybrids outperform either alone?
7. How should confidence be estimated per observation, per feature, and per reconstructed pixel?
8. What reference metrics and perceptual review protocol best distinguish genuine recovered detail from sharpened, hallucinated, or temporally unstable detail?
9. What architecture can remain hardware-vendor agnostic while still using modern GPU acceleration efficiently?
10. What is the best real-time/offline split for a future open Temporal Forge implementation?

## Nonclaims at closure

This repository does **not** claim:

- that FSR 4.1 is a poor game upscaler;
- that FSR 4.1 can never be adapted successfully to video;
- that optical flow is unnecessary in a future custom system;
- that machine learning is unnecessary or required;
- that future-frame reconstruction must use interpolation;
- that the current motion experiments disprove multi-frame super-resolution;
- that temporal reconstruction is inferior to spatial scaling in general;
- that the successor architecture has already been selected.

The successor effort begins by researching those choices without treating this repository's FSR-specific assumptions as defaults.
