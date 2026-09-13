# FFW-T0-2 — Determinism envelope evidence

**Date:** 2026-09-13 · **Task:** Gate-0 R2 · **Status:** complete

## Setup

- Input: `tests/sample.mp4` (640×360@30 h264, content SHA-256 recorded in the
  identity manifest) under the default control configuration.
- 3 repeated launches, identical configuration, 6 dumped FSR-output frames
  each after 2 warmup frames, native INT8 path on RX 7900 GRE.
- Verifier: `benchmarks/gate0/determinism_probe.py` (contract-tested in
  `tests/test_gate0_determinism_probe.py`).

## Measured envelope

**Verdict: `metric_stable` — not byte-identical.**

| pair | compared | byte-identical | max MAE (0-255) |
|---|---|---|---|
| det-00 vs det-01 | 6 | 2 | 0.000105 |
| det-01 vs det-02 | 6 | 2 | 0.000106 |

Per-frame pixel detail (run0 vs run1, full table in
`pixel_diff_details_run0_run1.json`):

- frames 0–1: byte-identical;
- frames 2–5: 38 / 255 / 627 / 652 differing 8-bit samples respectively out of
  6,220,800 per frame (0.0006 %–0.010 %), every difference exactly ±1 gray
  level, spatially clustered around moving content. Behaviour is consistent
  with sub-LSB FP16/atomic accumulation rounding differences in the neural
  compute path reaching an 8-bit rounding boundary, not with input or state
  divergence (identical dumps for the earliest frames, no drift growth).

## Comparison rule recorded for later A/B testing

1. Frame payload hashes are valid provenance keys ONLY within a single run;
   across runs, byte-identity is expected for static content but not for
   motion.
2. Arm-to-arm spatial deltas must exceed the run-to-run noise envelope —
   recorded MAE threshold 0.05 gray levels (≈480× the observed maximum
   run-to-run MAE) — before a difference is attributable to the experimental
   control. Deltas below the envelope are noise.
3. Temporal-metric comparisons inherit the same envelope; any A/B verdict
   must state its delta and the envelope together.

## Uncertainties

- The exact hardware source of the ±1-LSB differences (FP16 accumulation
  order, cooperative-matrix scheduling, or readback race) is not isolated;
  isolating it is downstream-campaign work and does not affect comparability
  under the recorded envelope.
- The envelope was measured on one scene/resolution tier (360p→1080p, native
  INT8). Other tiers inherit the rule but their envelopes were not measured
  in Gate 0.
