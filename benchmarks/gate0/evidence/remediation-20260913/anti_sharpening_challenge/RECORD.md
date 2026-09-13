# Remediation Task C — live anti-sharpening false-positive challenge

**Date:** 2026-09-13 · **Scope:** Gate-0 remediation Task C · **Status:** complete

## Adversarial hypothesis (fixed before execution — no parameter search)

A standard 100 % unsharp mask (3×3 box high-pass, ~1 px radius) applied to a
bilinear baseline recovers apparent acutance — a naive detail-energy-match
signal prefers it over the baseline — while adding no source-correlated
high-frequency reconstruction information: it only amplifies the baseline's
own edges (overshoot). Parameters (`unsharp_amount=1.0`, radius 1 px,
`searched_or_tuned=false`) were recorded in `run_sharpening_challenge.py`
before execution and run once.

## Setup (terminal tier)

- Source frame: `bbb_branches_1920x1080_medium_crf23.mp4`, decoder frame 20
  (same input clip as the terminal-tier determinism evidence).
- Reference: ffmpeg lanczos 3840×2160 (`/tmp/remediation/t0c/reference.ppm`).
- Baseline: ffmpeg bilinear 3840×2160 (`/tmp/remediation/t0c/baseline.ppm`).
- Metrics: existing Final Word evaluator policy
  (`benchmarks/gate0/evaluator_policy.py`), stride 2, 0–255 scale.

## Result

| signal | baseline | adversarial candidate |
|---|---|---|
| naive detail-match score (abs Δ detail energy vs reference; lower is better) | 0.1420 | **0.0803 — candidate preferred** |
| detail energy (reference 1.2479) | — | 1.3282 (ratio **1.064 > 1.050 tolerance**) |
| fidelity MAE vs reference | 0.6998 | 0.7166 (worse) |

**Evaluator verdict: REJECT**, with the dedicated anti-sharpening condition
(`unsupported_detail`) firing live for the first time on real terminal-tier
payloads — closing the gap this task targeted (prior live sharpening
candidates were rejected by the ordinary fidelity gate alone):

```text
reasons:
- fidelity: candidate MAE 0.7166 is not better than baseline MAE 0.6998 ...
- unsupported detail: candidate detail energy 1.3282 exceeds reference 1.2479
  (ratio 1.064 > 1.050)
```

The rejection reason is the appropriate one: the candidate's apparent acutance
gain is overshoot beyond the reference envelope, not source-correlated
reconstruction.

**Legitimate acceptance case (evaluator is not a blanket rejector):** a
candidate blended 0.6 toward the reference — genuinely closer to the reference
with detail energy inside the envelope — is **ACCEPTED**
(`legitimate_acceptance_case.evaluator_verdict = "accept"` in the report).
No acceptance criterion was weakened.

## Honest scope note

The full `sharpening_only` conjunction (fidelity win **and** overshoot) still
did not occur live: on this content no unsharp amount improves MAE against the
lanczos reference (consistent with the original Gate-0 parameter probe). That
conjunction remains demonstrated by contract tests
(`tests/test_gate0_evaluator_policy.py`); the live gap closed by this task is
the dedicated unsupported-detail rejection.

## Provenance

- Report: `challenge_report.json` (construction, parameters, all metrics,
  candidate payload SHA-256s).
- Tooling: `benchmarks/gate0/run_sharpening_challenge.py` (added; Task-C-only).
- Payloads: `/tmp/remediation/t0c/*.ppm` and the constructed candidates are
  reproducible from the recorded commands; hashes are in the report.
- Reproduction:
  `ffmpeg -i benchmarks/video_corpus/clips/bbb_branches_1920x1080_medium_crf23.mp4 -vf "select='eq(n,20)',scale=3840:2160:flags=lanczos" -fps_mode passthrough -frames:v 1 reference.ppm` (and `flags=bilinear` → `baseline.ppm`), then
  `python3 -m benchmarks.gate0.run_sharpening_challenge --reference reference.ppm --baseline baseline.ppm --report report.json`.
