# Temporal Forge current state

**Status:** CURRENT — EMPIRICAL VIABILITY VERDICT REPAIR IN PROGRESS
**As of:** 2026-09-14
**Source:** branch `empirical-viability-sprint` (Repair-A; CP-E verdict suspended)

This is a concise snapshot of what is true now. It is not an experiment
journal; active work is described in
[`../active/EMPIRICAL_VIABILITY_SPRINT.md`](../active/EMPIRICAL_VIABILITY_SPRINT.md).

## Project

Temporal Forge Player is a GPU-native local-video player. It keeps a strict
one-input-frame to one-output-frame relationship and performs temporal
reconstruction without frame generation, interpolation, or cadence conversion.
Runtime requires Vulkan 1.3.

## Research-surface reset

The current active research campaign is the empirical viability sprint. Its
purpose is empirical viability testing with reproducible measurements. The
frozen 64-cell primary capture is complete. The original CP-E verdict is
suspended because its causal residual evaluation used M2 before selecting the
strongest measured deterministic spatial comparator. Repair-A re-evaluated the
same captures with M1 selected by aggregate dominance; its provisional result
is **EMPIRICAL FAILURE**. Independent artifact publication and adjudication
remain before the repaired verdict is final. AMD parity/oracle work remains
deferred.
Historical quality, portability, and Final Word campaign material is
preserved under `docs/archive/` and the Final Word archive branch.

## Backend default (truth)

The default backend is FSR4-RE Experimental INT8 (proof-gated;
`SettingsStore` default with `allowExperimentalAsDefault = true`), default
selection on supported RDNA3, falling back on failure. The FSR 3.1.5 SDK tier
is a compiled-out stub in every build of this tree (`TFORGE_HAVE_FSR3_SDK`
is never defined); the reliability floor is the always-available spatial
fallback. See
[`../reference/ARCHITECTURE.md`](../reference/ARCHITECTURE.md).

## Verified versus unresolved

- The prior M6 triage gate record is preserved in
  [`../archive/plans/M6_REGRESSION_TRIAGE_20260902.md`](../archive/plans/M6_REGRESSION_TRIAGE_20260902.md).
- Existing evidence supports keeping reconstruction and final delivery
  dimensions as separate controls. It does not justify a universal 3x default.
- Current code, intended architecture, and dated evidence are not
  interchangeable. When they diverge, the active plan and audit must name the
  divergence.
- The empirical viability sprint's primary evidence is committed under
[`../../benchmarks/empirical/results/evs-20260914/`](../../benchmarks/empirical/results/evs-20260914/);
the active authority records the checkpoint history, bounded visual review, and
Repair-A corrected evaluation. The original CP-E interpretation is historical
until Repair-C. No reconstruction-quality change was made.

The worktree may contain untracked capture-generated evidence while the
quality campaign runs; that evidence is outside this documentation snapshot.

## Quality-lab policy (truth)

The checked-in `config/quality_lab.json` profile (base-only composition,
bilinear base filter) is loaded at startup and is the **shipped, measured
default playback policy** — applied scale-aware, only at ≥3x scale
(`PlaybackEngine`). `TFORGE_QUALITY_LAB_CONFIG` designates a deliberate
experiment override that is honored at every scale. This is not a hidden
diagnostic.

## Boundaries

Current code, dated evidence, and plans are distinct. Code is executable
truth; where documents disagree with it, the code wins and the documents get
fixed (the subject of this campaign).
