# Forge's Final Word — Gate 0: experiment-system trustworthiness

**Status:** EVIDENCE COMPLETE — INDEPENDENT AUDIT PENDING

**As of:** 2026-09-13

**Campaign branch:** `forge's-final-word-campaign`

**Scope:** Gate 0 only — establish a reproducible evidence spine proving the
experiment machinery is trustworthy enough for terminal adjudication. This plan
does not improve FSR reconstruction quality and does not start the terminal
adjudication experiments themselves.

## Authority and relationships

- This is the active plan for the Forge's Final Word (FFW) campaign Gate 0,
  assigned by the campaign goal. It is the single authority for Gate-0 work.
- [`QUALITY_CAMPAIGN.md`](QUALITY_CAMPAIGN.md) remains the separate authority
  for the shared quality-campaign recapture. Gate 0 must not modify that
  campaign's capture machinery, manifests, or evidence.
- The Expected-Food terminal sprint was prepared but never started
  ([`../reports/20260912_EXPECTED_FOOD_TERMINAL_SPRINT_PREPARATION.md`](../reports/20260912_EXPECTED_FOOD_TERMINAL_SPRINT_PREPARATION.md)).
  Gate 0 is the instrumentation-trustworthiness precondition for any later
  terminal adjudication; downstream gates stay locked until the Gate-0
  independent audit passes.

## Objective (evidence spine, R1–R6)

Establish reproducible evidence proving:

- **R1 identity** — the exact code, binary, shaders, model assets, runtime
  configuration, and relevant resources used by an experiment are identifiable.
- **R2 determinism** — repeated execution of the same controlled input is
  sufficiently deterministic for comparison, with a measured envelope.
- **R3 generation tracing** — frame, resource, recurrent-state, and producer
  generations can be traced well enough to detect stale or mismatched state.
- **R4 control liveness** — every experimental control used in later A/B
  testing is proven to affect the active execution path, not to be a no-op or
  stale configuration.
- **R5 evaluator validity** — the evaluator rejects deliberately invalid
  "improvements": wrong-phase reconstruction, unsupported detail, and
  sharpening-only gains.
- **R6 auditability** — all Gate-0 evidence is captured without undocumented
  operator knowledge, organized for an independent audit.

## Non-goals (hard exclusions)

No FSR reconstruction-quality work; no AMD parity oracle; no GT-A/GT-B/GT-C;
no motion-vector refinement, depth, jitter optimization, color experimentation,
postpass reverse engineering, hybrid composition, or performance optimization.
Gate-0 instrumentation may touch these code paths only in trace-only,
behavior-preserving form.

## Environment facts (verified 2026-09-13)

- Player binary (fresh integrated build, source ≙ `285a5788f`):
  `/tmp/temporal-forge-integrated-build/temporal_forge_player` (built
  2026-09-12). `build-fast/temporal_forge_player` also exists but its CMake
  cache is stale (obsolete checkout path); do not use it as evidence without
  binary-identity verification.
- GPU: AMD RDNA3 (vendor 0x1002), Vulkan 1.4.354, driver 26.2.2.
- Wayland session reachable (`wayland-0`); live player launches are possible
  from this environment.

## Tasks

Execution order: T0-1 → T0-3 → T0-4 → T0-2 → T0-5 → T0-6. Each task ends with
implementation + executed validation + captured evidence + recorded
uncertainties before the next begins.

### FFW-T0-1 — Experiment identity spine (R1)

Build `benchmarks/gate0/experiment_identity.py` (library + CLI) producing an
`experiment_identity` manifest (schema `temporal_forge.gate0.identity.v1`):
git head/dirty-state/diff content hash; binary path+SHA-256; shader source
hashes; model weight asset identity; input media SHA-256 + stream metadata;
full `TFORGE_*` runtime configuration; quality-lab config hash; host/GPU/driver
identity; and a fail-closed cross-check against the player's runtime trace
(`temporal_forge.runtime_pipeline.v1`: binary_sha256, git_head,
config_sha256). Contract test `tests/test_gate0_identity_manifest.py`.
Validation: one real player launch producing both artifacts, cross-check
passing, gaps (if any) recorded.

### FFW-T0-3 — Generation and state tracing (R3)

Make frame → {resource generation, recurrent/history reset generation, producer
identity, seek generation} traceable per frame, and prove stale/mismatched
state is detected. Reuse the existing event trace (`TFORGE_FSR4_DUMP_EVENT_TRACE`)
and dispatch trace; add only what is missing (trace-only, no behavior change).
Fail-closed precedents to verify and cite: dense-replay dimension guard,
`TemporalFrameContinuity` reset, seek quarantine. Validation: a live capture
containing a scene cut and a seek showing generations advancing, plus contract
tests for any new trace fields.

### FFW-T0-4 — Control liveness proofs (R4)

Build `benchmarks/gate0/control_liveness.py` with an explicit registry of the
controls later A/B testing will use (motion estimator/ablation, jitter mode,
CAS strength/disable, history confidence threshold, integrated temporal arm,
quality-lab blend/strength, presentation scaler). For each: paired launches
(default vs set) on a controlled input; liveness = (a) the setting is visible
in the runtime/dispatch/event trace AND (b) output payloads differ (hash or
metric delta ≠ 0). Emit a liveness report (schema
`temporal_forge.gate0.liveness.v1`); contract test for the verifier. Any
no-op or stale control is a Gate-0 defect to record.

### FFW-T0-2 — Determinism envelope (R2)

Build `benchmarks/gate0/determinism_probe.py`: deterministic synthetic input
(generated, content-hashed); K repeated runs, identical configuration and
identity manifest; per-frame output SHA-256 comparison across runs; if not
byte-identical, spatial/temporal metric variance quantifies the envelope. Emit
`temporal_forge.gate0.determinism.v1` report with the comparison rule later
A/B testing must use. Validation: report from ≥3 repeated runs + recorded
verdict.

### FFW-T0-5 — Evaluator negative controls (R5)

Build `benchmarks/gate0/evaluator_policy.py` + a negative-control suite. From a
real candidate/reference pair on the controlled input, construct: (a)
wrong-phase evaluation (frame N vs reference N±1 — aligned must win); (b)
sharpening-only "gain" (unsharp-masked bilinear control must not classify as a
reconstruction win); (c) unsupported detail (high-frequency injection must not
win). The evaluator policy must return explicit rejection reasons. Failing
negative controls are recorded as Gate-0 defects; none may be silently waived.

### FFW-T0-6 — Audit bundle (R6)

Assemble `benchmarks/gate0/audit/<date>/` with all Gate-0 artifacts, hashes,
and a generated index; publish
`docs/reports/20260913_FFW_GATE0_EVIDENCE_REPORT.md` mapping R1–R6 → evidence
paths → exact reproduction commands; update this plan with results and
remaining uncertainties. Gate-0 completion additionally requires an independent
review verdict (outside this plan's execution); the bundle must make that
review possible without undocumented operator knowledge.

## Results log

| Task | Status | Evidence | Uncertainties |
|---|---|---|---|
| FFW-T0-1 | complete 2026-09-13 | `benchmarks/gate0/experiment_identity.py`, `tests/test_gate0_identity_manifest.py` (13 pass), live launch record + manifest + runtime trace in `benchmarks/gate0/evidence/FFW-T0-1/` (cross-check passed: binary_sha256, git_head, config_sha256, run_id) | Generic v4.1 weight blob not installed on this host (native INT8 packs hashed instead — production path); git dirty=True at capture because Gate-0 tooling itself was untracked (dirty path list recorded in manifest) |
| FFW-T0-3 | complete 2026-09-13 | `benchmarks/gate0/evidence/FFW-T0-3/` (scene-cut live capture: generations advance at the cut, stable elsewhere; `tests/test_gate0_state_generation_trace.py` 5 pass; CTest 23/23) | Live seek / live realloc generation-change captures unreachable headlessly (seek is QML-only; playlist advance skipped in headless); covered by contract tests. Pre-existing Python failures (3) confirmed on clean tree: M6 retention contract vs retained `review_harness/images` payloads — filed for quality-campaign authority |
| FFW-T0-4 | complete 2026-09-13 | `benchmarks/gate0/control_liveness.py`, `tests/test_gate0_control_liveness.py`, evidence `benchmarks/gate0/evidence/FFW-T0-4/` — 8/8 controls `live_output` (trace-visible in both arms AND outputs differ) | Defect found & fixed: runtime trace misreported default jitter as enabled (trace-only fix + contract test). Ablation env doubles as estimator selector (producer `off+ablation:<kind>`) — documented, not a defect |
| FFW-T0-2 | complete 2026-09-13 | `benchmarks/gate0/determinism_probe.py`, `tests/test_gate0_determinism_probe.py`, evidence `benchmarks/gate0/evidence/FFW-T0-2/` — verdict `metric_stable`: not byte-identical; run-to-run max MAE 0.000105 (0-255), ±1-LSB isolated samples on moving content only; comparison rule recorded (noise envelope 0.05) | Envelope measured on one tier (360p→1080p native INT8); exact HW source of ±1-LSB differences not isolated (downstream; does not affect comparability) |
| FFW-T0-5 | complete 2026-09-13 | `benchmarks/gate0/evaluator_policy.py`, `run_evaluator_controls.py`, `tests/test_gate0_evaluator_policy.py` (10 pass), evidence `benchmarks/gate0/evidence/FFW-T0-5/` — 18/18 invalid candidates rejected on real frames; phase sanity `phase_sensitive` (0.754 vs 5.196 MAE) | Live sharpening-only construct never wins fidelity on this content (probe 0.25–1.0) — those arms are rejected via the fidelity gate; the dedicated sharpening-only rule is contract-tested on synthetic frames only |
| FFW-T0-6 | complete 2026-09-13 | `benchmarks/gate0/audit_bundle.py`, bundle `benchmarks/gate0/audit/20260913/` (index + checklist; 8/8 acceptance rules pass, no missing artifacts), report `docs/reports/20260913_FFW_GATE0_EVIDENCE_REPORT.md` | Independent audit verdict pending — recorded in `audit_index.json → independent_review`; downstream gates stay locked until it exists |

## Completion criteria

1. All six tasks implemented, validated, and evidenced per their sections. ✅ 2026-09-13
2. Every R1–R6 requirement has primary evidence reachable from the audit
   bundle index. ✅ 2026-09-13
3. All defects found (no-op controls, nondeterminism beyond the recorded
   envelope, evaluator blind spots) are recorded with disposition. ✅ One
   trace-provenance defect found and fixed (R4); all other findings recorded
   as uncertainties or filed findings.
4. Remaining uncertainties explicitly recorded. ✅ Plan results log +
   evidence report §"Remaining uncertainties and filed findings".
5. Independent audit verdict pending — recorded as such; downstream gates stay
   locked until it exists. ⏳ PENDING — the audit bundle
   (`benchmarks/gate0/audit/20260913/`) is the review input.

## Risks

- Live capture may be blocked by environment permission boundaries (prior
  campaigns needed escalated execution). If blocked, this is a genuine blocker
  to record — sandbox-only aborts are not quality evidence.
- GPU contention from user activity is recorded as provenance and must never be
  treated as an image-quality or determinism result; user processes are never
  paused or terminated.
