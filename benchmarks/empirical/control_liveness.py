"""Empirical control liveness proofs.

Schema ``temporal_forge.empirical.liveness.v1``. For every experimental control
that later A/B testing relies on, this probe proves the control actually
affects the active execution path: the setting must be visible in the
player's own traces (runtime trace, per-frame event trace, or dispatch-trace
log lines) — expected in the *set* arm, and the *default* state expected in
the *default* arm — and the captured output payloads are compared between
the arms.

Verdicts:
- ``live_output``      — trace-visible and output payloads differ;
- ``live_trace_only``  — trace-visible but outputs identical on this input
                         (recorded: this input masks the pixel effect);
- ``no_op``            — a trace check failed: the control is a no-op or
                         stale configuration (Empirical defect);
- ``inconclusive``     — an arm failed to capture; the control is unproven.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from benchmarks.empirical.player_run import run_player

SCHEMA = "temporal_forge.empirical.liveness.v1"

# Dispatch-trace log lines are needed by log-based checks; the flag is
# trace-only and never alters reconstruction.
BASE_ENV = {"TFORGE_FSR4_DISPATCH_TRACE": "1"}


@dataclass(frozen=True)
class TraceCheck:
    """One trace-visibility assertion for one arm."""

    arm: str  # "default" | "set"
    kind: str  # "runtime_trace_field" | "event_trace_field" | "log_contains"
    field_path: str  # dotted path for JSON checks; ignored for log checks
    expected: Any  # JSON value, or literal substring for log checks


@dataclass(frozen=True)
class Control:
    name: str
    env: dict[str, str]
    checks: list[TraceCheck] = field(default_factory=list)


def _lookup(document: dict[str, Any], dotted: str) -> Any:
    node: Any = document
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node


def _event_values(output_dir: Path, field_path: str) -> set[str]:
    events = sorted(Path(output_dir).glob("events/event_trace_*.json"))
    return {
        json.dumps(
            _lookup(json.loads(p.read_text(encoding="utf-8")), field_path)
        )
        for p in events
    }


def _check_passes(check: TraceCheck, record: dict[str, Any]) -> bool:
    if check.kind == "log_contains":
        log = Path(record["player_log"])
        return log.is_file() and check.expected in log.read_text(
            encoding="utf-8", errors="replace"
        )
    if check.kind == "runtime_trace_field":
        trace = json.loads(
            Path(record["runtime_trace_path"]).read_text(encoding="utf-8")
        )
        return _lookup(trace, check.field_path) == check.expected
    if check.kind == "event_trace_field":
        return json.dumps(check.expected) in _event_values(
            Path(record["output_dir"]), check.field_path
        )
    raise ValueError(f"unknown check kind: {check.kind}")


def dumps_differ(
    default_record: dict[str, Any], set_record: dict[str, Any]
) -> bool | None:
    """True when any common dump payload differs between the two arms."""
    common = set(default_record["dumps"]) & set(set_record["dumps"])
    if not common:
        return None
    return any(
        default_record["dumps"][name] != set_record["dumps"][name] for name in common
    )


def verify_control(
    control: Control,
    default_record: dict[str, Any],
    set_record: dict[str, Any],
) -> dict[str, Any]:
    """Pure verifier for one control pair; contract-tested without a player."""
    arms_ok = bool(default_record.get("ok")) and bool(set_record.get("ok"))
    check_results = [
        {
            "arm": check.arm,
            "kind": check.kind,
            "field_path": check.field_path,
            "expected": check.expected,
            "passed": _check_passes(
                check, set_record if check.arm == "set" else default_record
            ),
        }
        for check in control.checks
    ]
    trace_visible = all(result["passed"] for result in check_results)
    differs = dumps_differ(default_record, set_record) if arms_ok else None

    if not arms_ok:
        verdict = "inconclusive"
    elif not trace_visible:
        verdict = "no_op"
    elif differs is True:
        verdict = "live_output"
    elif differs is False:
        verdict = "live_trace_only"
    else:
        verdict = "inconclusive"

    return {
        "control": control.name,
        "env": control.env,
        "default_arm_ok": bool(default_record.get("ok")),
        "set_arm_ok": bool(set_record.get("ok")),
        "checks": check_results,
        "output_differs": differs,
        "verdict": verdict,
    }


# --- the registry of controls later A/B testing relies on ------------------


def default_registry() -> list[Control]:
    """Controls exercised by Empirical; extend only with evidence-backed need."""
    return [
        Control(
            name="motion_ablation_zero",
            env={"TFORGE_FSR4_MOTION_ABLATION": "zero"},
            checks=[
                # The ablation selector doubles as the estimator selector, so
                # the payload arm runs with the estimator off — the producer
                # label records exactly that.
                TraceCheck("set", "event_trace_field",
                           "stateGenerations.motionProducer",
                           "off+ablation:zero"),
                TraceCheck("default", "event_trace_field",
                           "stateGenerations.motionProducer", "codec_refined"),
            ],
        ),
        Control(
            name="motion_ablation_block",
            env={"TFORGE_FSR4_MOTION_ABLATION": "block"},
            checks=[
                TraceCheck("set", "event_trace_field",
                           "stateGenerations.motionProducer",
                           "off+ablation:block"),
                TraceCheck("default", "event_trace_field",
                           "stateGenerations.motionProducer", "codec_refined"),
            ],
        ),
        Control(
            name="motion_estimator_off",
            env={"TFORGE_FSR4_MOTION_ESTIMATOR": "off"},
            checks=[
                TraceCheck("set", "event_trace_field",
                           "stateGenerations.motionProducer", "off"),
                TraceCheck("default", "event_trace_field",
                           "stateGenerations.motionProducer", "codec_refined"),
            ],
        ),
        Control(
            name="jitter_synthetic",
            env={"TFORGE_FSR4_JITTER_MODE": "synthetic"},
            checks=[
                TraceCheck("set", "runtime_trace_field", "jitter_enabled", True),
                TraceCheck("default", "runtime_trace_field", "jitter_enabled", False),
            ],
        ),
        Control(
            name="cas_disabled",
            env={"TFORGE_FSR4_DISABLE_CAS": "1"},
            checks=[
                TraceCheck("set", "runtime_trace_field", "cas_enabled", False),
                TraceCheck("default", "runtime_trace_field", "cas_enabled", True),
            ],
        ),
        Control(
            name="history_confidence_threshold_035",
            env={"TFORGE_FSR4_HISTORY_CONFIDENCE_THRESHOLD": "0.35"},
            checks=[
                TraceCheck("set", "log_contains", "", "threshold=0.35"),
                TraceCheck("default", "log_contains", "", "threshold=0.55"),
            ],
        ),
        Control(
            name="color_history_enabled",
            env={"TFORGE_FSR4_ENABLE_COLOR_HISTORY": "1"},
            checks=[
                TraceCheck("set", "runtime_trace_field", "history_enabled", True),
                TraceCheck("default", "runtime_trace_field", "history_enabled", False),
            ],
        ),
        Control(
            name="recurrent_enabled",
            env={"TFORGE_FSR4_ENABLE_RECURRENT": "1"},
            checks=[
                TraceCheck("set", "runtime_trace_field", "recurrent_enabled", True),
                TraceCheck("default", "runtime_trace_field", "recurrent_enabled", False),
            ],
        ),
    ]


def run_liveness(
    player_path: Path,
    input_media: Path,
    output_dir: Path,
    controls: list[Control] | None = None,
    frames: int = 6,
    warmup: int = 2,
    timeout_s: int = 120,
) -> dict[str, Any]:
    """Run every registered control as a paired default/set capture."""
    controls = default_registry() if controls is None else controls
    results = []
    for control in controls:
        default_record = run_player(
            player_path, input_media, output_dir / control.name / "default",
            run_id=f"EVS-4-{control.name}-default", frames=frames,
            warmup=warmup, env_overrides=dict(BASE_ENV), timeout_s=timeout_s,
        )
        set_record = run_player(
            player_path, input_media, output_dir / control.name / "set",
            run_id=f"EVS-4-{control.name}-set", frames=frames,
            warmup=warmup,
            env_overrides={**BASE_ENV, **control.env}, timeout_s=timeout_s,
        )
        results.append(verify_control(control, default_record, set_record))
    summary: dict[str, int] = {"live_output": 0, "live_trace_only": 0,
                               "no_op": 0, "inconclusive": 0}
    for result in results:
        summary[result["verdict"]] += 1
    return {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "player": str(player_path),
        "input_media": str(input_media),
        "frames": frames,
        "warmup": warmup,
        "controls": results,
        "summary": summary,
    }


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Run Empirical control-liveness paired captures."
    )
    parser.add_argument("--player", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args(argv)

    report = run_liveness(
        player_path=Path(args.player),
        input_media=Path(args.input),
        output_dir=Path(args.output_dir),
        frames=args.frames,
        warmup=args.warmup,
        timeout_s=args.timeout,
    )
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True), encoding="utf-8"
    )
    print(json.dumps(report["summary"], sort_keys=True))
    return 0 if report["summary"]["no_op"] == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
