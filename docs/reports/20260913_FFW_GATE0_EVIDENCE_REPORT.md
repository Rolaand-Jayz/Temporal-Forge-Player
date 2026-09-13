# Forge's Final Word — Gate 0 evidence report

**Date:** 2026-09-13
**Campaign branch:** `forge's-final-word-campaign`
**Plan:** [`docs/active/FFW_GATE0_PLAN.md`](../active/FFW_GATE0_PLAN.md)
**Audit bundle:** `benchmarks/gate0/audit/20260913/audit_index.json` (+ `AUDIT_CHECKLIST.md`)

This report maps the six Gate-0 evidence-spine requirements to their primary
evidence and exact reproduction paths. The audit bundle records bundle-time
SHA-256 hashes of every artifact listed here and re-verifies all
machine-checkable acceptance rules; the independent review verdict is to be
recorded in the bundle's `independent_review` field.

## R1 — Experiment identity

Every experiment run can now be bound to its exact code, binary, shaders,
model assets, input, and configuration. `benchmarks/gate0/experiment_identity.py`
emits schema `temporal_forge.gate0.identity.v1` (git head/dirty/diff hash;
binary SHA-256; 23 shader sources; 13 native INT8 packs; input media SHA-256 +
stream metadata; full `TFORGE_*` environment; host/GPU identity) and applies a
**fail-closed** cross-check against the player's own runtime trace — empty or
mismatched provenance fails, and the current checkout is never substituted for
a recorded value (mirroring `campaign_provenance.py`).

Evidence: `benchmarks/gate0/evidence/FFW-T0-1/` (live launch, cross-check
passed on binary_sha256, git_head, config_sha256, run_id). Every later Gate-0
capture carries the same manifest (`FFW-T0-2/`, `FFW-T0-3/`, `FFW-T0-4/`).

## R2 — Determinism envelope

Three repeated runs, identical configuration (`benchmarks/gate0/evidence/FFW-T0-2/`):
verdict **`metric_stable`** — not byte-identical. Run-to-run differences are
exactly ±1 gray level in 0.0006 %–0.010 % of samples, clustered on moving
content; max MAE 0.000105 (0–255 scale). Recorded comparison rule: arm
deltas must exceed the 0.05 envelope before being attributable to a control;
payload hashes are provenance keys within a run only.

## R3 — Generation and state tracing

The per-frame event trace now carries `stateGenerations` (seek generation,
scene-cut count, history-reset count, committed temporal-reset count,
uploader allocation generation, motion-producer identity) and
`resourceGeometry`. Live scene-cut capture (`benchmarks/gate0/evidence/FFW-T0-3/`):
all generations advance exactly at the detector scene cut and are stable
elsewhere. Stale-state precedents verified: continuity reset, seek
quarantine, dense-replay dimension fail-closed, all contract-tested.

## R4 — Control liveness

`benchmarks/gate0/control_liveness.py` proves each A/B control reaches the
execution path (trace-visible in both arms) AND changes output payloads.
Result: **8/8 controls `live_output`**, zero no-ops
(`benchmarks/gate0/evidence/FFW-T0-4/liveness_report.json`).

**Defect found and fixed:** the runtime trace reported `jitter_enabled=true`
for the default (unset) jitter mode although the effective selection is
`JitterMode::Off`. `writeRuntimePipelineTrace` now mirrors the effective
selection and records `requested_jitter_mode` (trace-only change; contract
test added). Documented behavior (not a defect): `TFORGE_FSR4_MOTION_ABLATION`
doubles as the estimator-mode selector, so ablation arms run with the
estimator off — producer labels record `off+ablation:<kind>`.

## R5 — Evaluator validity

`benchmarks/gate0/evaluator_policy.py` + `run_evaluator_controls.py`:
on real captured frames, **18/18 deliberately invalid candidates rejected**
(wrong-phase, unsupported-detail, sharpening-only constructions), and the
phase-sanity self-check proves the metric separates aligned from off-by-one
content by 6.9× MAE. The dedicated sharpening-only rule (fidelity win with
overshoot) is contract-covered; on this content no sharpening arm wins
fidelity, so live sharpening arms are rejected by the fidelity gate — recorded
as an honest limitation, not waived.

## R6 — Auditability

`benchmarks/gate0/audit_bundle.py` assembles `benchmarks/gate0/audit/20260913/`:
every artifact hashed at bundle time, eight acceptance rules re-verified
(all passing), and `AUDIT_CHECKLIST.md` gives the exact reproduction commands.

**Review classification (remediation 2026-09-13):** the 2026-09-13 review in
the bundle (`INDEPENDENT_REVIEW.md`) is a **same-session adversarial
self-review**. It is retained as reproduction evidence and as supporting
material for a future genuinely independent auditor. It does **not** satisfy
the campaign's builder → independent auditor → Sol adjudicator separation and
must not be represented as the campaign's final independent Gate-0 audit.

> **Gate-0 implementation and internal adversarial verification are complete.
> Final independent audit and campaign adjudication remain pending.**

**FINAL INDEPENDENT AUDIT: PENDING**
**SOL GATE-0 ADJUDICATION: PENDING**

## Test state at close

- Gate-0 contract suites: 34 tests passing across four files
  (`test_gate0_*.py`).
- Full runnable CTest: 23/23 (5 skipped/disabled by configuration).
- Python contracts: 303 passing; **3 pre-existing failures confirmed on the
  clean tree** (see below).

## Remaining uncertainties and filed findings

1. **Pre-existing (filed for the quality-campaign authority):** the merged
   branch retains `review_harness/images/*.png` payloads while
   `test_m6_quality_class_annotations` and two `test_review_harness_contract`
   tests assert they were purged — the branch contradicts the M6 retention
   contract. Confirmed identical on the clean tree; not repaired inside Gate 0.
2. Live seek / live render-size-change generation advances are not reachable
   headlessly (seek is QML-invoked; playlist advance is skipped in headless
   mode); covered by contract tests and the scene-cut live capture.
3. The determinism envelope was measured on one scene/resolution tier
   (360p→1080p, native INT8); other tiers inherit the comparison rule but
   their envelopes were not measured in Gate 0.
4. The generic v4.1 weight blob is not installed on this host; the native
   INT8 packs (production path) are fully hashed. The generic FP16 fallback
   path cannot be asset-identity-verified here.
5. The ±1-LSB run-to-run differences are not attributed to an exact hardware
   mechanism; they are bounded by the recorded envelope.
6. **No independent Gate-0 audit verdict has been issued.** The retained
   `independent_review.json` records only the same-session internal
   verification outcome (`PASS`) and explicitly states that it does not satisfy
   builder → independent auditor → Sol adjudicator separation. Final
   independent audit and downstream gate unlocking remain pending.
