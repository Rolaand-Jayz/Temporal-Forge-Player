# FFW-T0-5 — Evaluator negative controls evidence

**Date:** 2026-09-13 · **Task:** Gate-0 R5 · **Status:** complete

## Evaluator policy

`benchmarks/gate0/evaluator_policy.py` — the adjudication rule set a candidate
must pass to be called an improvement: fidelity win (MAE vs reference, with
margin) AND detail energy within the reference envelope (+5 % tolerance).
Overshoot beyond the reference envelope is "unsupported detail"; a fidelity
win that coexists with overshoot is a "sharpening-only gain". Every verdict
carries its rule outcomes and reasons for audit.

## Phase-sanity self-check (live)

Aligned pairs vs phase-shifted pairs on real frames
(`tests/sample.mp4` frames 2–7; reference = ffmpeg lanczos 1920×1080,
baseline = ffmpeg bilinear 1920×1080):

```text
verdict: phase_sensitive
aligned mean MAE: 0.7539 (baseline vs its own reference)
shifted mean MAE: 5.1959 (baseline vs next frame's reference)  → 6.9× separation
```

The metric demonstrably distinguishes frame N from frame N±1 on real
payloads — the negative control "wrong-phase reconstruction" cannot hide.

## Live negative controls

18/18 invalid candidates REJECTED (6 frames × 3 controls), report in
`negative_controls_report.json`:

| control | construction | outcome |
|---|---|---|
| wrong_phase | neighbouring frame's content offered as frame N | rejected via fidelity gate on every frame |
| unsupported_detail | ±16 checker pattern injected on baseline | rejected via unsupported-detail rule on every frame |
| sharpening_only | unsharp-masked baseline (amount 1.5) | rejected via fidelity gate on every frame |

## Honest limitations (recorded, not waived)

1. **The live sharpening-only construct never wins fidelity on this content.**
   A parameter probe (amounts 0.25–1.0) showed unsharp-masked bilinear
   monotonically worsens MAE against the lanczos reference while increasing
   detail energy; every live sharpening arm is therefore rejected by the
   fidelity gate alone. The dedicated `sharpening_only` rule (fidelity win +
   overshoot) is exercised by contract tests
   (`tests/test_gate0_evaluator_policy.py`) on synthetic frames; no real-world
   capture in Gate 0 demonstrated a fidelity-winning sharpening arm.
2. The reference convention (ffmpeg lanczos upscale of the source) is the
   campaign's standing reference identity, not ground truth; the evaluator
   adjudicates relative to it exactly as later A/B adjudication will.
3. Metrics are pure Python MAE / 4-neighbour Laplacian energy on the G channel
   with stride 2 (recorded in every report). The repo's full SSIM machinery is
   not re-implemented here; Gate 0 validates the decision rules, not the
   metric library internals (covered by their own suites).
