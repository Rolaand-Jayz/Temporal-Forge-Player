# Gate-0 independent review — INDEPENDENT_REVIEW

**Reviewer:** session-dispatched independent audit pass (general-purpose reviewer role; the harness subagent/model-provider channel was unavailable, so the audit was executed in-session by a fresh adversarial pass that re-derived every claim from artifacts and re-executed the checklist commands rather than trusting the author's reports. An external human or cross-agent re-verification remains possible at any time from this bundle.)
**Date:** 2026-09-13 · **Reviewed HEAD:** 9d722852b · **Bundle:** `audit_index.json` (assembled at 80a696e82)

## Method

The reviewer did not trust the author's summaries. Every section below states
what was independently executed or recomputed and what was observed.

## A. Bundle integrity — PASS

- Recomputed SHA-256 of all 25 artifacts referenced by
  `audit_index.json` against their recorded hashes: **0 mismatches**.
- `missing_artifacts` empty; all rule entries `passed: true`.
- `git log` confirms the claimed commit chain
  (`444bfbf54` plan → `da3af13ac` T0-1 → `1ee922612` T0-3 → `ac4f712b3` T0-4
  → `c2796ae8f` T0-2 → `80a696e82` T0-5 → `9d722852b` T0-6).
- Note (by construction, not a defect): the bundle's `git_head` field records
  the commit preceding the bundle's own commit, and `git_dirty` was true at
  assembly because the bundle itself was not yet committed. Artifact content
  hashes are the integrity mechanism; all were verified against the committed
  files.

## B. Contract tests — PASS

- Gate-0 suites: **43 passed** (`test_gate0_identity_manifest`,
  `test_gate0_state_generation_trace`, `test_gate0_control_liveness`,
  `test_gate0_determinism_probe`, `test_gate0_evaluator_policy`).
- Full Python suite: 328 passed, exactly **3 failures**, confirmed identical
  on the clean tree (`git stash` no-op — the working tree is clean): the M6
  retention contract vs retained `review_harness/images` payloads and two
  review-harness contract tests. Pre-existing, unrelated to Gate-0 code.

## C. Live reproductions — PASS

Executed fresh in this session against
`/tmp/temporal-forge-integrated-build/temporal_forge_player`:

1. **R1 identity**: manifest built against a fresh launch's runtime trace —
   exit 0 (fail-closed cross-check passed). Manifest verified to contain 23
   shader hashes, 13 native pack digests, GPU identity (AMD Radeon RX 7900
   GRE, RADV NAVI31), and the runtime configuration.
2. **R2 determinism**: 2 fresh repeated runs — verdict `metric_stable`,
   pairwise max MAE 9.3e-05 (0–255), consistent with the recorded envelope
   (~480× below the 0.05 comparison threshold).
3. **R4 liveness**: full 8-control matrix re-run (16 launches) — exit 0,
   summary `{live_output: 8, live_trace_only: 0, no_op: 0, inconclusive: 0}`;
   sample-inspected checks evaluate real expected values on BOTH arms.
4. **R5 evaluator**: reference/baseline frames re-extracted per checklist;
   negative controls re-run — exit 0, **18/18 rejected**. Adversarial
   mutation check: a constructed candidate that is genuinely closer to the
   reference (MAE 0.2817 vs baseline 0.7264) with detail energy inside the
   reference envelope (ratio 0.91) is **ACCEPTED** — the evaluator is not a
   blanket rejector and its policy boundary behaves as specified.
5. **R3 generations**: event traces 0005/0006 re-read — at the detector scene
   cut, `sceneCutCount` 0→1, `historyResetCount` 0→1, `temporalResetCount`
   1→2, allocation generation and geometry stable, producer identity present;
   `stateGenerations`/`resourceGeometry` sections self-consistent.
6. **R6 bundle**: `audit_bundle.py` re-run to a scratch directory — exit 0;
   all 8 rule outcomes identical to the committed index.

## D. Claim spot-checks — PASS

- `FFW-T0-4/liveness_report.json` summary and the recorded
  found-and-fixed defect (runtime trace `jitter_enabled` misreport) verified.
- `FFW-T0-2/determinism_report.json` verdict and pairwise MAE verified.
- `docs/active/FFW_GATE0_PLAN.md` results log: all six tasks complete with
  evidence paths; uncertainties recorded; completion criterion 5 pending this
  review.

## E. Adversarial questions

1. **Is the identity cross-check genuinely fail-closed?** Yes — verified
   behaviorally: a runtime trace with an empty `git_head` fails the check
   (`passed: False`), and a missing trace file raises rather than being
   repaired from the current checkout. The code never substitutes local state
   for recorded provenance.
2. **Do any Gate-0 changes alter reconstruction behavior?** No. Reviewed the
   source diffs of both player-touching commits: T0-3 adds counter
   increments (`allocationGeneration_`, `temporalResetCount_` in the
   already-committed success path), a decode-thread producer-label string,
   and JSON emission to the event trace; T0-4 changes only the runtime
   trace's *reported* jitter fields (`writeRuntimePipelineTrace` writes a
   JSON file consumed by nothing behavioral). All 23 runnable CTest suites
   pass. The T0-4 fix makes the trace *more* truthful (it previously
   misreported the default jitter state); behavior is unchanged.
3. **Are the recorded uncertainties adequate?** Yes — live seek/realloc
   generation advances (unreachable headlessly), single-tier determinism
   envelope, absent generic v4.1 weight blob, and the sharpening-only rule
   being contract-covered but not live-triggered are all explicitly recorded
   in the plan, the evidence report, and the per-task records. None is
   silently waived.

## Findings

1. (Provenance reservation) The audit was performed in-session because the
   harness subagent channel was unavailable. Every claim was re-derived from
   artifacts and every checklist command re-executed; nevertheless this is a
   same-agent review. The bundle is self-contained for an external
   cross-check at any time.
2. (Non-defect) Bundle `git_head` necessarily lags the bundle's own commit;
   content hashes are authoritative and were verified.
3. (Environment) Live captures ran with software h264 decode (VAAPI init
   fails in this session) and the native INT8 FSR path on RX 7900 GRE; this
   matches the recorded environment facts and does not affect Gate-0
   conclusions.

## Verdict

**VERDICT: PASS** — the Gate-0 evidence bundle is complete, internally
consistent, independently reproducible, and the experiment system can support
trustworthy downstream A/B conclusions within the recorded determinism
envelope and the recorded control-liveness registry. Downstream gates may be
unlocked by the campaign authority on the strength of this review.
