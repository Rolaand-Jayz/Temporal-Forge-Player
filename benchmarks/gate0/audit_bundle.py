"""Gate-0 audit bundle assembler.

Schema ``temporal_forge.gate0.audit.v1``. Collects every Gate-0 evidence
artifact into one index with bundle-time SHA-256 hashes, re-verifies each
machine-checkable acceptance rule (identity cross-checks passed, no no-op
controls, determinism envelope recorded, all negative controls rejected), and
emits an auditor checklist mapping the six evidence-spine requirements to
artifacts and exact reproduction commands.

An auditor needs only this index, the repository at the recorded commit, and
the commands in the checklist — no undocumented operator knowledge.
"""

from __future__ import annotations

import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from benchmarks.gate0.experiment_identity import sha256_file

SCHEMA = "temporal_forge.gate0.audit.v1"

REPO_ROOT = Path(__file__).resolve().parents[2]


def _requirement_artifacts() -> dict[str, list[dict[str, str]]]:
    """R1-R6 → evidence artifacts (paths relative to the repository root)."""
    return {
        "R1_identity": [
            {"path": "benchmarks/gate0/experiment_identity.py",
             "role": "identity manifest builder (library + CLI)"},
            {"path": "tests/test_gate0_identity_manifest.py",
             "role": "identity contract tests"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-1/experiment_identity.json",
             "role": "live identity manifest, cross-check passed"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-1/runtime_trace.json",
             "role": "player runtime pipeline trace from the same launch"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-1/launch_record.json",
             "role": "launch record + environment facts"},
        ],
        "R2_determinism": [
            {"path": "benchmarks/gate0/determinism_probe.py",
             "role": "determinism probe (library + CLI)"},
            {"path": "tests/test_gate0_determinism_probe.py",
             "role": "determinism contract tests"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-2/determinism_report.json",
             "role": "3-run determinism report"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-2/pixel_diff_details_run0_run1.json",
             "role": "per-pixel diff characterization"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-2/RECORD.md",
             "role": "envelope + A/B comparison rule"},
        ],
        "R3_generation_tracing": [
            {"path": "tests/test_gate0_state_generation_trace.py",
             "role": "trace contract tests (player source contracts)"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-3/RECORD.md",
             "role": "scene-cut generation evidence record"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-3/events/event_trace_0006.json",
             "role": "scene-cut frame with advancing generations"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-3/experiment_identity.json",
             "role": "identity manifest for the trace capture"},
        ],
        "R4_control_liveness": [
            {"path": "benchmarks/gate0/control_liveness.py",
             "role": "liveness probe + control registry"},
            {"path": "tests/test_gate0_control_liveness.py",
             "role": "verdict-rule contract tests"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-4/liveness_report.json",
             "role": "8-control paired liveness report"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-4/launch_record.json",
             "role": "defects found and dispositions"},
        ],
        "R5_evaluator_validity": [
            {"path": "benchmarks/gate0/evaluator_policy.py",
             "role": "evaluator policy (library + CLI)"},
            {"path": "benchmarks/gate0/run_evaluator_controls.py",
             "role": "negative-control constructor/runner"},
            {"path": "tests/test_gate0_evaluator_policy.py",
             "role": "rejection-rule contract tests"},
            {"path": "benchmarks/gate0/evidence/FFW-T0-5/negative_controls_report.json",
             "role": "live negative-control report on real frames"},
        ],
        "R6_audit": [
            {"path": "benchmarks/gate0/audit_bundle.py", "role": "this tool"},
            {"path": "docs/active/FFW_GATE0_PLAN.md",
             "role": "Gate-0 plan with results log and uncertainties"},
            {"path": "docs/reports/20260913_FFW_GATE0_EVIDENCE_REPORT.md",
             "role": "Gate-0 evidence report"},
        ],
    }


def _check_acceptance_rules() -> list[dict[str, Any]]:
    """Machine-checkable Gate-0 acceptance rules over the evidence files."""
    rules: list[dict[str, Any]] = []

    def add(name: str, passed: bool, detail: str) -> None:
        rules.append({"rule": name, "passed": bool(passed), "detail": detail})

    for manifest_path in sorted(
        REPO_ROOT.glob("benchmarks/gate0/evidence/FFW-T0-*/experiment_identity.json")
    ):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        cross = manifest.get("runtime_trace_crosscheck", {})
        add(
            f"identity_crosscheck[{manifest_path.parent.name}]",
            cross.get("passed") is True,
            f"binary/git/config provenance cross-checked: {cross.get('passed')}",
        )

    liveness_path = REPO_ROOT / "benchmarks/gate0/evidence/FFW-T0-4/liveness_report.json"
    if liveness_path.is_file():
        report = json.loads(liveness_path.read_text(encoding="utf-8"))
        summary = report.get("summary", {})
        add(
            "liveness_no_noop_controls",
            summary.get("no_op") == 0 and summary.get("inconclusive") == 0,
            f"summary={summary}",
        )
        add(
            "liveness_all_controls_live",
            summary.get("live_output", 0) == len(report.get("controls", [])),
            f"{summary.get('live_output')}/{len(report.get('controls', []))} live_output",
        )

    determinism_path = REPO_ROOT / "benchmarks/gate0/evidence/FFW-T0-2/determinism_report.json"
    if determinism_path.is_file():
        report = json.loads(determinism_path.read_text(encoding="utf-8"))
        verdict = report.get("verification", {}).get("verdict")
        add(
            "determinism_envelope_recorded",
            verdict in ("byte_identical", "metric_stable"),
            f"verdict={verdict}",
        )

    controls_path = REPO_ROOT / "benchmarks/gate0/evidence/FFW-T0-5/negative_controls_report.json"
    if controls_path.is_file():
        report = json.loads(controls_path.read_text(encoding="utf-8"))
        summary = report.get("summary", {})
        add(
            "negative_controls_all_rejected",
            summary.get("total", 0) > 0
            and summary.get("rejected") == summary.get("total"),
            f"summary={summary}",
        )
    return rules


def build_bundle(output_dir: Path) -> dict[str, Any]:
    """Assemble the audit index with bundle-time hashes and rule checks."""
    requirements: dict[str, Any] = {}
    missing: list[str] = []
    for requirement, artifacts in _requirement_artifacts().items():
        entries = []
        for artifact in artifacts:
            path = REPO_ROOT / artifact["path"]
            entry = {**artifact, "exists": path.is_file()}
            if path.is_file():
                entry["sha256"] = sha256_file(path)
                entry["size_bytes"] = path.stat().st_size
            else:
                missing.append(artifact["path"])
            entries.append(entry)
        requirements[requirement] = entries

    rules = _check_acceptance_rules()
    head = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )
    status = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "status", "--porcelain"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )
    bundle = {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "repo_root": str(REPO_ROOT),
        "git_head": head.stdout.strip(),
        "git_dirty": bool(status.stdout.strip()),
        "requirements": requirements,
        "acceptance_rules": rules,
        "missing_artifacts": missing,
        "all_rules_passed": all(r["passed"] for r in rules) and not missing,
        "independent_review": {
            "status": "pending",
            "note": (
                "Gate-0 completion requires an independent review verdict that "
                "the experiment system can support trustworthy downstream "
                "conclusions. This bundle is the review input; downstream "
                "gates stay locked until the verdict is recorded here."
            ),
        },
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "audit_index.json").write_text(
        json.dumps(bundle, indent=2, sort_keys=True), encoding="utf-8"
    )
    return bundle


def write_checklist(bundle: dict[str, Any], output_dir: Path) -> None:
    """Human audit checklist: requirement → artifacts → reproduction commands."""
    lines = [
        "# Gate-0 audit checklist",
        "",
        f"Generated: {bundle['generated_utc']} · git_head: `{bundle['git_head']}`",
        "",
        "Reproduce each verification from the repository root:",
        "",
        "```bash",
        "# R1 identity: rebuild a manifest for a fresh launch and require the",
        "# fail-closed runtime-trace cross-check to pass (exit 0)",
        "python3 -m benchmarks.gate0.experiment_identity --player <player> \\",
        "    --output /tmp/identity.json --input tests/sample.mp4 \\",
        "    --runtime-trace <runtime_trace.json>",
        "",
        "# R2 determinism: 3 repeated runs must land in the recorded envelope",
        "python3 -m benchmarks.gate0.determinism_probe --player <player> \\",
        "    --input tests/sample.mp4 --output-dir /tmp/t0-2 \\",
        "    --report /tmp/determinism.json --runs 3",
        "",
        "# R4 liveness: every registered control must be live (exit 0)",
        "python3 -m benchmarks.gate0.control_liveness --player <player> \\",
        "    --input tests/sample.mp4 --output-dir /tmp/t0-4 \\",
        "    --report /tmp/liveness.json",
        "",
        "# R5 evaluator: every negative control must be rejected (exit 0)",
        "python3 -m benchmarks.gate0.run_evaluator_controls \\",
        "    --reference-dir <refs> --baseline-dir <bases> \\",
        "    --report /tmp/negative.json",
        "",
        "# Contract tests (all Gate-0 suites)",
        "python3 -m pytest tests/test_gate0_*.py -q",
        "```",
        "",
        "## Acceptance rules re-verified at bundle time",
        "",
    ]
    for rule in bundle["acceptance_rules"]:
        mark = "PASS" if rule["passed"] else "FAIL"
        lines.append(f"- [{mark}] `{rule['rule']}` — {rule['detail']}")
    if bundle["missing_artifacts"]:
        lines.append("")
        lines.append("## Missing artifacts")
        lines.extend(f"- `{p}`" for p in bundle["missing_artifacts"])
    lines.append("")
    lines.append("## Independent review")
    lines.append("")
    lines.append(bundle["independent_review"]["note"])
    (output_dir / "AUDIT_CHECKLIST.md").write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Assemble the Gate-0 audit bundle."
    )
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args(argv)

    bundle = build_bundle(Path(args.output_dir))
    write_checklist(bundle, Path(args.output_dir))
    print(json.dumps({
        "all_rules_passed": bundle["all_rules_passed"],
        "missing": len(bundle["missing_artifacts"]),
        "rules": {r["rule"]: r["passed"] for r in bundle["acceptance_rules"]},
    }, indent=1, sort_keys=True))
    return 0 if bundle["all_rules_passed"] else 2


if __name__ == "__main__":
    sys.exit(main())
