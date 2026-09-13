"""Gate-0 evaluator negative controls on real captured payloads.

Constructs three deliberately invalid "improvements" from real
reference/baseline frame sets and requires the evaluator policy to reject
every one of them:

- ``wrong_phase``        — the neighbouring frame's content is offered as the
  reconstruction of frame N;
- ``unsupported_detail`` — a high-frequency checker pattern the reference does
  not contain is injected onto the baseline;
- ``sharpening_only``    — an unsharp-masked baseline (overshoot energy, no
  new correspondence information).

Also runs the evaluator's phase-sanity self-check on the reference set. A
control that is NOT rejected is a Gate-0 evaluator defect and fails the run.
"""

from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from benchmarks.gate0.evaluator_policy import (
    SCHEMA,
    adjudicate,
    load_frame,
    phase_sanity,
)


def _inject_checker(frame: dict[str, Any], amplitude: int = 16) -> dict[str, Any]:
    """Superimpose a deterministic 2x2 checker pattern on all channels."""
    width, height = frame["width"], frame["height"]
    payload = bytearray(frame["rgb"])
    for y in range(height):
        for x in range(width):
            delta = amplitude if (x // 2 + y // 2) % 2 == 0 else -amplitude
            i = (y * width + x) * 3
            for c in range(3):
                payload[i + c] = max(0, min(255, payload[i + c] + delta))
    return {"width": width, "height": height, "rgb": bytes(payload),
            "path": "constructed:unsupported_detail"}


def _unsharp(frame: dict[str, Any], amount: float = 1.5) -> dict[str, Any]:
    """Unsharp mask: frame + amount * (frame - 3x3 box blur)."""
    width, height = frame["width"], frame["height"]
    src = frame["rgb"]
    payload = bytearray(len(src))
    for y in range(height):
        row = y * width
        y_up = (y - 1) * width if y > 0 else row
        y_down = (y + 1) * width if y < height - 1 else row
        for x in range(width):
            x_left = x - 1 if x > 0 else x
            x_right = x + 1 if x < width - 1 else x
            i = (row + x) * 3
            for c in range(3):
                blur = (
                    src[(y_up + x) * 3 + c]
                    + src[(y_down + x) * 3 + c]
                    + src[(row + x_left) * 3 + c]
                    + src[(row + x_right) * 3 + c]
                    + 4 * src[i + c]
                ) / 8.0
                value = src[i + c] + amount * (src[i + c] - blur)
                payload[i + c] = max(0, min(255, round(value)))
    return {"width": width, "height": height, "rgb": bytes(payload),
            "path": "constructed:sharpening_only"}


def run_controls(reference_paths: list[Path], baseline_paths: list[Path],
                 stride: int = 2) -> dict[str, Any]:
    references = [load_frame(p) for p in reference_paths]
    baselines = [load_frame(p) for p in baseline_paths]
    if len(references) != len(baselines) or not references:
        raise ValueError("reference and baseline frame sets must be non-empty and equal-length")

    sanity = phase_sanity(references, baselines, stride)

    controls = []
    for index, (reference, baseline) in enumerate(zip(references, baselines)):
        # wrong_phase: the next frame's reference content stands in for this
        # frame's reconstruction. The last frame uses the previous one.
        neighbour_index = (
            index + 1 if index + 1 < len(references) else index - 1
        )
        candidates = {
            "wrong_phase": references[neighbour_index],
            "unsupported_detail": _inject_checker(baseline),
            "sharpening_only": _unsharp(baseline),
        }
        for kind, candidate in candidates.items():
            verdict = adjudicate(reference, baseline, candidate, stride)
            controls.append({
                "frame_index": index,
                "reference": reference["path"],
                "baseline": baseline["path"],
                "control": kind,
                "candidate": candidate["path"],
                "verdict": verdict["verdict"],
                "rejected": verdict["verdict"] == "reject",
                "reasons": verdict["reasons"],
                "rule_outcomes": verdict["rule_outcomes"],
                "metrics": verdict["metrics"],
            })

    rejected = sum(1 for c in controls if c["rejected"])
    return {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "stride": stride,
        "phase_sanity": sanity,
        "controls": controls,
        "summary": {
            "total": len(controls),
            "rejected": rejected,
            "accepted": len(controls) - rejected,
        },
    }


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Run Gate-0 evaluator negative controls on real frames."
    )
    parser.add_argument("--reference-dir", required=True,
                        help="directory of reference_*.ppm frames")
    parser.add_argument("--baseline-dir", required=True,
                        help="directory of baseline_*.ppm frames")
    parser.add_argument("--report", required=True)
    parser.add_argument("--stride", type=int, default=2)
    args = parser.parse_args(argv)

    reference_paths = sorted(Path(args.reference_dir).glob("reference_*.ppm"))
    baseline_paths = sorted(Path(args.baseline_dir).glob("baseline_*.ppm"))
    report = run_controls(reference_paths, baseline_paths, args.stride)
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "summary": report["summary"],
        "phase_sanity": report["phase_sanity"]["verdict"],
    }))
    return 0 if report["summary"]["rejected"] == report["summary"]["total"] else 2


if __name__ == "__main__":
    sys.exit(main())
