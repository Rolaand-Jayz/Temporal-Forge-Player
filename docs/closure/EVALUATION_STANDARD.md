# Temporal Forge Evaluation Standard

**Status:** CANONICAL FOR FSR-ERA CLOSURE  
**Effective:** 2026-09-15

This standard defines how research and engineering claims in the closed FSR-centered Temporal Forge repository are evaluated. It is intentionally stricter than a demonstration-oriented project README: a working path is not enough; a retained claim must be traceable to reproducible evidence and must survive plausible alternative explanations.

## 1. Evidence classes

Every consequential statement should be identifiable as one of:

- **Measured fact** — directly produced by a preserved run, trace, artifact, source inspection, hash, metric, or test.
- **Observation** — direct human or instrument observation whose scope is stated.
- **Inference** — interpretation supported by evidence but not directly measured.
- **Hypothesis** — falsifiable proposed explanation awaiting adequate test.
- **Unresolved** — evidence is insufficient, conflicting, invalidated, or outside the tested scope.
- **Project decision** — an architectural or product choice informed by evidence; not itself presented as a scientific fact.

## 2. Required claim chain

A major research claim should, where applicable, identify:

1. the claim;
2. the tested code/binary identity;
3. the input/reference identity;
4. the controlled variable;
5. comparison/control arms;
6. runtime configuration and relevant environment selectors;
7. captured outputs, traces, or measurements;
8. provenance sufficient to reproduce or audit the run;
9. plausible competing explanations;
10. invalid or excluded evidence;
11. the resulting disposition and its scope.

A missing element does not automatically invalidate all evidence, but it limits the strength of the conclusion and must not be silently inferred.

## 3. Disposition vocabulary

Major claims use these dispositions:

- **SUPPORTED** — repeatedly consistent with valid evidence in the tested scope.
- **SUPPORTED WITH QUALIFICATION** — supported only under explicit conditions or with material limitations.
- **NOT SUPPORTED** — tested evidence does not provide the claimed benefit or relationship.
- **CONTRADICTED IN TESTED SCOPE** — valid evidence directly opposes the claim in the tested scope.
- **UNRESOLVED** — insufficient or conflicting evidence.
- **INVALIDATED EVIDENCE** — a run or conclusion failed provenance, reference, runtime, visual, instrumentation, or other validity requirements and cannot support the claim.
- **PROJECT DECISION** — evidence-informed direction choice that deliberately does not overstate empirical certainty.

## 4. Promotion gate

A behavior or quality change must not be promoted merely because it runs, improves one metric, or looks better in one scene. Promotion requires appropriate evidence across:

- correct method identity;
- source/reference provenance;
- representative scenes/resolutions for the claim;
- appropriate spatial and/or temporal controls;
- runtime trace sufficient to prove the intended path executed;
- artifact and failure review;
- performance measurement when performance is part of the claim;
- visual review when the artifact class is not safely captured by the metric alone;
- explicit handling of regressions and contradictory slices.

A conditional win must remain conditional.

## 5. Negative evidence

Failed experiments are retained when they constrain the architecture. A result that disproves an expected benefit is not project failure; it is evidence. Negative results must not be erased by later summaries merely because a subsequent experiment took a different direction.

Examples in this repository include motion-estimator upgrades that failed to produce repeatable reconstruction gains, rejected midpoint/interpolation paths, and quality evidence invalidated after human review detected periodic lattice corruption.

## 6. Human review versus automated metrics

Automated metrics are necessary but not sufficient for artifact classes they do not reliably detect. The repository preserves the case where an automated periodic score passed while human review found a visible lattice; the evidence was reopened and the prior campaign status invalidated. Human review therefore has authority to reopen an apparently green automated gate when a concrete visible defect is demonstrated.

Human review is not a substitute for provenance or controls. It complements them.

## 7. Reproducibility and portability

A public technical claim must not depend silently on maintainer-specific paths, untracked local assets, undocumented dependencies, or unverifiable binaries. Where optional proprietary, reverse-engineered, or locally provisioned assets are required, that dependency and rights/provenance status must be explicit.

The completed clean-clone portability campaign is part of the closure evidence for this requirement.

## 8. Independent/adversarial verification

For high-impact closure or remediation claims, independent review should attempt to falsify the result rather than merely confirm the implementation. Review findings must be either remediated or explicitly adjudicated. A review loop that finds defects does not count as a clean qualification loop.

## 9. Architecture-change standard

A pivot does not require proof that the previous architecture can never succeed. It requires enough evidence to show that continued investment in that architecture is no longer the strongest evidence-supported use of effort for the project objective.

The FSR-era closure therefore makes a bounded claim: the accumulated Temporal Forge evidence does not justify continuing to treat FSR 4.1 adaptation as the project's architectural center. It does not claim impossibility of future FSR-based video reconstruction.

## 10. Closure requirements

An experimental era is closed only when:

- its current state is updated;
- stale active plans are removed from active authority;
- the causal technical history records why the direction ended;
- major claims have dispositions;
- limitations and unresolved questions are explicit;
- the repository no longer instructs agents to continue the closed campaign;
- future work is prevented from inheriting the closed architecture as an unexamined assumption.

This closure set exists to satisfy those requirements.
