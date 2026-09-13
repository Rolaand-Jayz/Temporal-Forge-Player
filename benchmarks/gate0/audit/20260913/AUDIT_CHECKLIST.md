# Gate-0 audit checklist

Generated: 2026-09-13T13:10:21Z · git_head: `80a696e823f482684c980f9ab044aa16eff241a7`

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

Gate-0 completion requires an independent review verdict that the experiment system can support trustworthy downstream conclusions. This bundle is the review input; downstream gates stay locked until the verdict is recorded here.