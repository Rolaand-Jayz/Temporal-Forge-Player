"""Gate-0 remediation Task C — live anti-sharpening false-positive challenge.

Adversarial hypothesis (fixed before execution; parameters are not tuned
against the evaluator): a standard 100 % unsharp mask (3×3 box high-pass,
~1 px radius) applied to a bilinear baseline recovers apparent acutance — a
naive detail-energy-match signal prefers it over the baseline — while adding
no source-correlated high-frequency information (it only amplifies the
baseline's own edges, i.e. overshoot).

The runner constructs that candidate once, scores it under the naive signal,
adjudicates it with the existing Final Word evaluator policy, and also runs a
legitimate acceptance case (a candidate genuinely closer to the reference with
detail energy inside the reference envelope) to demonstrate the evaluator is
not a blanket rejector. All payloads are 3840×2160 PPM frames at the terminal
tier geometry.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

from benchmarks.gate0.experiment_identity import sha256_file
from benchmarks.gate0.evaluator_policy import (
    SCHEMA,
    adjudicate,
    detail_energy,
    fidelity,
    load_frame,
)

UNSHARP_AMOUNT = 1.0
BLEND_TOWARD_REFERENCE = 0.6


def _unsharp(frame: dict, amount: float = UNSHARP_AMOUNT) -> dict:
    """Unsharp mask: frame + amount × (frame − 3×3 box blur)."""
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
            "path": f"constructed:unsharp_mask_amount_{amount}"}


def _blend(a: dict, b: dict, weight_a: float) -> dict:
    out = bytearray(len(a["rgb"]))
    for i, (va, vb) in enumerate(zip(a["rgb"], b["rgb"])):
        out[i] = round(va * weight_a + vb * (1 - weight_a))
    return {**b, "rgb": bytes(out),
            "path": f"constructed:blend_weight_{weight_a}"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run the Gate-0 anti-sharpening adversarial challenge."
    )
    parser.add_argument("--reference", required=True,
                        help="reference PPM (lanczos terminal-tier upscale)")
    parser.add_argument("--baseline", required=True,
                        help="baseline PPM (bilinear terminal-tier upscale)")
    parser.add_argument("--report", required=True)
    args = parser.parse_args(argv)

    reference = load_frame(Path(args.reference))
    baseline = load_frame(Path(args.baseline))

    ref_detail = detail_energy(reference)
    base_detail = detail_energy(baseline)

    # Adversarial candidate: fixed-parameter unsharp mask (no search).
    candidate = _unsharp(baseline, UNSHARP_AMOUNT)
    cand_detail = detail_energy(candidate)

    # Naive detail-energy-match signal: distance of the candidate's detail
    # energy from the reference's. A sharpness-seeking reviewer using only
    # this signal would prefer the adversarial candidate.
    naive_baseline = abs(base_detail - ref_detail)
    naive_candidate = abs(cand_detail - ref_detail)

    adversarial_verdict = adjudicate(reference, baseline, candidate, stride=2)

    # Legitimate acceptance case: strictly closer to the reference, detail
    # inside the reference envelope.
    acceptance_candidate = _blend(reference, baseline, BLEND_TOWARD_REFERENCE)
    acceptance_verdict = adjudicate(reference, baseline, acceptance_candidate,
                                    stride=2)

    report = {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "tier": "terminal (1920x1080 source -> 3840x2160 evaluation geometry)",
        "reference": {"path": args.reference, "sha256": sha256_file(Path(args.reference))},
        "baseline": {"path": args.baseline, "sha256": sha256_file(Path(args.baseline))},
        "adversarial": {
            "hypothesis": (
                "unsharp masking recovers apparent acutance without "
                "source-correlated reconstruction information"
            ),
            "construction": "baseline + amount * (baseline - box3x3(baseline))",
            "parameters": {"unsharp_amount": UNSHARP_AMOUNT, "radius_px": 1,
                           "searched_or_tuned": False},
            "candidate_sha256": __import__("hashlib").sha256(
                candidate["rgb"]).hexdigest(),
            "naive_detail_match_signal": {
                "metric": "abs(detail_energy - reference_detail_energy)",
                "baseline_score": naive_baseline,
                "candidate_score": naive_candidate,
                "candidate_preferred_by_naive_signal":
                    naive_candidate < naive_baseline,
                "reference_detail_energy": ref_detail,
                "baseline_detail_energy": base_detail,
                "candidate_detail_energy": cand_detail,
            },
            "fidelity": {
                "candidate_mae": adversarial_verdict["metrics"]["candidate_mae"],
                "baseline_mae": adversarial_verdict["metrics"]["baseline_mae"],
            },
            "evaluator_verdict": adversarial_verdict["verdict"],
            "rule_outcomes": adversarial_verdict["rule_outcomes"],
            "rejection_reasons": adversarial_verdict["reasons"],
        },
        "legitimate_acceptance_case": {
            "construction": f"blend(reference, baseline, weight_a={BLEND_TOWARD_REFERENCE})",
            "candidate_sha256": __import__("hashlib").sha256(
                acceptance_candidate["rgb"]).hexdigest(),
            "evaluator_verdict": acceptance_verdict["verdict"],
            "metrics": acceptance_verdict["metrics"],
            "rule_outcomes": acceptance_verdict["rule_outcomes"],
        },
    }
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "adversarial_verdict": report["adversarial"]["evaluator_verdict"],
        "naive_signal_prefers_candidate":
            report["adversarial"]["naive_detail_match_signal"][
                "candidate_preferred_by_naive_signal"],
        "rejection_reasons": report["adversarial"]["rejection_reasons"],
        "acceptance_verdict":
            report["legitimate_acceptance_case"]["evaluator_verdict"],
    }, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
