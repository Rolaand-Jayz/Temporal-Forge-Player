"""Gate-0 evaluator policy and negative controls.

Schema ``temporal_forge.gate0.evaluator.v1``. The evaluator decides whether a
candidate reconstruction arm may be called an improvement over a baseline.
It is deliberately conservative: a candidate is accepted only when it wins on
fidelity against the reference AND does not "win" by manufacturing detail the
reference does not support.

Rejection rules (each is a Gate-0 negative control):
- ``wrong_phase``        — a candidate whose content belongs to a neighbouring
  frame scores clearly worse on fidelity than the aligned candidate; the
  evaluator must reject it (phase-blindness is an evaluator defect).
- ``unsupported_detail`` — candidate high-frequency energy exceeding the
  reference by more than ``hf_tolerance`` is rejected even when fidelity
  improves: detail beyond the reference is not reconstruction.
- ``sharpening_only``    — a candidate whose fidelity gain coexists with
  overshoot-driven high-frequency energy is rejected as a sharpening-only
  gain; sharpening is a presentation choice, not reconstruction quality.

All metrics are pure Python over 8-bit RGB with a documented stride so an
auditor can recompute them from the retained payloads.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from benchmarks.gate0.determinism_probe import read_ppm

SCHEMA = "temporal_forge.gate0.evaluator.v1"

# Evaluation sampling stride (x and y) for the 0-255 scale metrics. A stride
# of 1 is exact; larger strides trade exactness for speed and must be recorded.
STRIDE = 2

# High-frequency (Laplacian magnitude) tolerance relative to the reference.
# A candidate may not exceed the reference's detail energy by more than this
# fraction without tripping the unsupported-detail rule.
HF_TOLERANCE = 0.05

# Fidelity (MAE) improvement the candidate must show over the baseline to
# count as a win at all, in 0-255 scale points.
FIDELITY_WIN_MARGIN = 0.05


def _sampled_pairs(a: bytes, b: bytes, stride: int) -> zip:
    return zip(a[::stride], b[::stride])


def mean_absolute_error(a: bytes, b: bytes, stride: int = STRIDE) -> float:
    total = sum(abs(x - y) for x, y in _sampled_pairs(a, b, stride))
    count = len(a[::stride])
    return total / count if count else 0.0


def laplacian_energy(rgb: bytes, width: int, height: int,
                     stride: int = STRIDE) -> float:
    """Mean 4-neighbour Laplacian magnitude over the luma proxy (G channel).

    The G channel is used as a deterministic luma proxy; a real transfer-aware
    luma is unnecessary for the relative detail-energy comparison this policy
    makes. Stride applies to both axes.
    """
    total = 0.0
    count = 0
    for y in range(1, height - 1, stride):
        row = y * width
        for x in range(1, width - 1, stride):
            i = (row + x) * 3 + 1
            center = rgb[i]
            total += abs(
                4 * center
                - rgb[i - 3]
                - rgb[i + 3]
                - rgb[i - width * 3]
                - rgb[i + width * 3]
            )
            count += 1
    return total / count if count else 0.0


def load_frame(path: Path) -> dict[str, Any]:
    width, height, payload = read_ppm(path)
    return {"width": width, "height": height, "rgb": payload, "path": str(path)}


def fidelity(reference: dict[str, Any], candidate: dict[str, Any],
             stride: int = STRIDE) -> float:
    return mean_absolute_error(reference["rgb"], candidate["rgb"], stride)


def detail_energy(frame: dict[str, Any], stride: int = STRIDE) -> float:
    return laplacian_energy(frame["rgb"], frame["width"], frame["height"], stride)


def adjudicate(
    reference: dict[str, Any],
    baseline: dict[str, Any],
    candidate: dict[str, Any],
    stride: int = STRIDE,
    hf_tolerance: float = HF_TOLERANCE,
    fidelity_margin: float = FIDELITY_WIN_MARGIN,
) -> dict[str, Any]:
    """Decide whether ``candidate`` is an admissible improvement over baseline.

    Returns a verdict plus every rule outcome so an auditor can see exactly
    why a candidate was accepted or rejected.
    """
    candidate_fidelity = fidelity(reference, candidate, stride)
    baseline_fidelity = fidelity(reference, baseline, stride)
    reference_detail = detail_energy(reference, stride)
    candidate_detail = detail_energy(candidate, stride)
    detail_ratio = (
        candidate_detail / reference_detail if reference_detail > 0 else float("inf")
    )

    fidelity_win = candidate_fidelity < baseline_fidelity - fidelity_margin
    unsupported_detail = detail_ratio > 1.0 + hf_tolerance
    sharpening_only = fidelity_win and unsupported_detail and (
        candidate_fidelity <= baseline_fidelity
    )
    accepted = fidelity_win and not unsupported_detail

    reasons = []
    if not fidelity_win:
        reasons.append(
            f"fidelity: candidate MAE {candidate_fidelity:.4f} is not better "
            f"than baseline MAE {baseline_fidelity:.4f} by the required margin"
        )
    if unsupported_detail:
        reasons.append(
            f"unsupported detail: candidate detail energy {candidate_detail:.4f} "
            f"exceeds reference {reference_detail:.4f} "
            f"(ratio {detail_ratio:.3f} > {1.0 + hf_tolerance:.3f})"
        )
    if sharpening_only:
        reasons.append(
            "sharpening-only gain: fidelity improved only together with "
            "overshoot-driven detail energy beyond the reference"
        )

    return {
        "verdict": "accept" if accepted else "reject",
        "reasons": reasons,
        "metrics": {
            "candidate_mae": candidate_fidelity,
            "baseline_mae": baseline_fidelity,
            "reference_detail_energy": reference_detail,
            "candidate_detail_energy": candidate_detail,
            "detail_ratio": detail_ratio,
            "stride": stride,
            "hf_tolerance": hf_tolerance,
            "fidelity_win_margin": fidelity_margin,
        },
        "rule_outcomes": {
            "fidelity_win": fidelity_win,
            "unsupported_detail": unsupported_detail,
            "sharpening_only": sharpening_only,
        },
    }


def phase_sanity(
    references: list[dict[str, Any]], baseline: dict[str, Any], stride: int = STRIDE
) -> dict[str, Any]:
    """Metric sanity probe: aligned fidelity must beat off-by-one fidelity.

    A metric that cannot tell frame N from frame N±1 is blind to phase and
    cannot adjudicate temporal reconstruction. This is a Gate-0 evaluator
    self-check, not a candidate verdict.
    """
    if len(references) < 2:
        return {"verdict": "inconclusive", "reason": "need at least 2 frames"}
    aligned = [
        fidelity(references[i], baseline, stride)
        for i in range(len(references))
    ]
    aligned_pairs_mean = sum(aligned) / len(aligned)
    shifted = [
        fidelity(references[i], baseline, stride)
        for i in range(1, len(references))
    ]
    shifted_pairs_mean = (
        sum(
            fidelity(references[i + 1], references[i], stride)
            for i in range(len(references) - 1)
        )
        / (len(references) - 1)
    )
    # The aligned reference pair (baseline vs its own reference) must score
    # clearly better than inter-frame (shifted) comparisons.
    phase_sensitive = shifted_pairs_mean > aligned_pairs_mean
    return {
        "verdict": "phase_sensitive" if phase_sensitive else "phase_blind",
        "aligned_mean_mae": aligned_pairs_mean,
        "shifted_mean_mae": shifted_pairs_mean,
        "stride": stride,
    }


def run_negative_controls(
    reference_frames: list[Path],
    baseline_frames: list[Path],
    construct_invalid: Any,
    stride: int = STRIDE,
) -> dict[str, Any]:
    """Execute the three invalid-"improvement" rejections on real payloads.

    ``construct_invalid(kind, reference_frame, baseline_frame)`` must return a
    candidate frame dict for one of: "wrong_phase", "unsupported_detail",
    "sharpening_only".
    """
    results = []
    for reference_path, baseline_path in zip(reference_frames, baseline_frames):
        reference = load_frame(reference_path)
        baseline = load_frame(baseline_path)
        for kind in ("wrong_phase", "unsupported_detail", "sharpening_only"):
            candidate = construct_invalid(kind, reference, baseline)
            verdict = adjudicate(reference, baseline, candidate, stride)
            results.append({
                "frame": reference_path.name,
                "control": kind,
                "verdict": verdict["verdict"],
                "reasons": verdict["reasons"],
                "metrics": verdict["metrics"],
            })
    rejected = sum(1 for r in results if r["verdict"] == "reject")
    return {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "controls": results,
        "summary": {
            "total": len(results),
            "rejected": rejected,
            "accepted": len(results) - rejected,
        },
    }


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Adjudicate one candidate frame against reference and baseline."
    )
    parser.add_argument("--reference", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--stride", type=int, default=STRIDE)
    args = parser.parse_args(argv)

    verdict = adjudicate(
        load_frame(Path(args.reference)),
        load_frame(Path(args.baseline)),
        load_frame(Path(args.candidate)),
        stride=args.stride,
    )
    print(json.dumps(verdict, indent=2))
    return 0 if verdict["verdict"] == "accept" else 2


if __name__ == "__main__":
    sys.exit(main())
