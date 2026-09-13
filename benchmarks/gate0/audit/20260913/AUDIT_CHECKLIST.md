# Gate-0 audit checklist

Generated: 2026-09-13T21:52:10Z · git_head: `dec86736ce89e7e26603631faff40cf9031ee7dc`

Reproduce each verification from the repository root:

```bash
# R1 identity: rebuild a manifest for a fresh launch and require the
# fail-closed runtime-trace cross-check to pass (exit 0)
python3 -m benchmarks.gate0.experiment_identity --player <player> \
    --output /tmp/identity.json --input tests/sample.mp4 \
    --runtime-trace <runtime_trace.json>

# R2 determinism: 3 repeated runs must land in the recorded envelope
python3 -m benchmarks.gate0.determinism_probe --player <player> \
    --input tests/sample.mp4 --output-dir /tmp/t0-2 \
    --report /tmp/determinism.json --runs 3

# R4 liveness: every registered control must be live (exit 0)
python3 -m benchmarks.gate0.control_liveness --player <player> \
    --input tests/sample.mp4 --output-dir /tmp/t0-4 \
    --report /tmp/liveness.json

# R5 evaluator: every negative control must be rejected (exit 0)
python3 -m benchmarks.gate0.run_evaluator_controls \
    --reference-dir <refs> --baseline-dir <bases> \
    --report /tmp/negative.json

# Remediation Task B: terminal-tier determinism (1080p -> 2160p,
# performance_2160 native graph; see the RECORD.md for the exact
# env_overrides) — verdict must be byte_identical or metric_stable
python3 -c "from pathlib import Path; import json; \
  from benchmarks.gate0.determinism_probe import run_determinism; \
  r = run_determinism(player_path=Path('<player>'), \
    input_media=Path('benchmarks/video_corpus/clips/bbb_branches_1920x1080_medium_crf23.mp4'), \
    output_dir=Path('/tmp/rem-t0b'), runs=3, frames=6, warmup=2, \
    timeout_s=300, env_overrides={'TFORGE_FSR4_FORCE_VIEWPORT': '3840x2160', \
    'TFORGE_FSR4_FORCE_SCALE': '2.0'}); \
    print(r['verification']['verdict'])"

# Remediation Task C: anti-sharpening adversarial challenge — the
# naive detail-match signal must prefer the unsharp candidate while
# the evaluator rejects it via unsupported detail; the legitimate
# blend candidate must be accepted
python3 -m benchmarks.gate0.run_sharpening_challenge \
    --reference <lanczos_3840x2160.ppm> --baseline <bilinear_3840x2160.ppm> \
    --report /tmp/challenge.json

# Contract tests (all Gate-0 suites)
python3 -m pytest tests/test_gate0_*.py -q
```

## Acceptance rules re-verified at bundle time

- [PASS] `identity_crosscheck[FFW-T0-1]` — binary/git/config provenance cross-checked: True
- [PASS] `identity_crosscheck[FFW-T0-2]` — binary/git/config provenance cross-checked: True
- [PASS] `identity_crosscheck[FFW-T0-3]` — binary/git/config provenance cross-checked: True
- [PASS] `identity_crosscheck[FFW-T0-4]` — binary/git/config provenance cross-checked: True
- [PASS] `liveness_no_noop_controls` — summary={'inconclusive': 0, 'live_output': 8, 'live_trace_only': 0, 'no_op': 0}
- [PASS] `liveness_all_controls_live` — 8/8 live_output
- [PASS] `determinism_envelope_recorded` — verdict=metric_stable
- [PASS] `negative_controls_all_rejected` — summary={'total': 18, 'rejected': 18, 'accepted': 0}

## Independent review

Review class: same_session_adversarial_self_review
Classification: Performed by the same agent session that built the Gate-0 evidence, acting in a reviewer role (the harness subagent/model-provider channel was unavailable). Retained as reproduction evidence and as supporting material for a future genuinely independent auditor. Does NOT satisfy the Final Word campaign's builder -> independent auditor -> Sol adjudicator separation.
Internal verification outcome: PASS
FINAL INDEPENDENT AUDIT: PENDING
SOL GATE-0 ADJUDICATION: PENDING
Date: 2026-09-13
Report: INDEPENDENT_REVIEW.md