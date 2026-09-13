"""Gate-0 R5 contract: evaluator policy rejects invalid "improvements".

Locked on tiny synthetic frames without a player: the fidelity/detail metric
semantics, the wrong-phase sanity probe, and each rejection rule —
unsupported detail and sharpening-only gains must never be classifiable as
reconstruction improvements.
"""

from __future__ import annotations

import unittest

from benchmarks.gate0.evaluator_policy import (
    adjudicate,
    laplacian_energy,
    mean_absolute_error,
    phase_sanity,
)


def _gray_frame(width: int, height: int, value: int) -> dict:
    rgb = bytes([value, value, value]) * (width * height)
    return {"width": width, "height": height, "rgb": rgb, "path": "synthetic"}


def _checker_frame(width: int, height: int, base: int, amp: int) -> dict:
    """Base value with a 2x2 high-frequency checkerboard superimposed."""
    payload = bytearray()
    for y in range(height):
        for x in range(width):
            v = base + (amp if (x // 2 + y // 2) % 2 == 0 else -amp)
            v = max(0, min(255, v))
            payload += bytes([v, v, v])
    return {"width": width, "height": height, "rgb": bytes(payload),
            "path": "synthetic"}


def _shifted_frame(frame: dict, offset: int) -> dict:
    """Shift content by ``offset`` gray levels — a stand-in for different-phase
    content in the pure-metric tests."""
    payload = bytes(
        max(0, min(255, b + offset))
        for b in frame["rgb"]
    )
    return {**frame, "rgb": payload}


class MetricSemanticsTests(unittest.TestCase):
    def test_identical_frames_have_zero_mae(self) -> None:
        a = _gray_frame(8, 8, 40)
        self.assertEqual(mean_absolute_error(a["rgb"], a["rgb"]), 0.0)

    def test_laplacian_energy_is_zero_on_flat_and_positive_on_checker(self) -> None:
        flat = _gray_frame(16, 16, 100)
        checker = _checker_frame(16, 16, 100, 20)
        self.assertEqual(laplacian_energy(flat["rgb"], 16, 16), 0.0)
        self.assertGreater(
            laplacian_energy(checker["rgb"], 16, 16), 0.0
        )


class PhaseSanityTests(unittest.TestCase):
    def test_metric_distinguishes_aligned_from_shifted_content(self) -> None:
        reference = _gray_frame(8, 8, 40)
        baseline = _gray_frame(8, 8, 42)
        other_phase = _gray_frame(8, 8, 90)
        result = phase_sanity([reference, other_phase], baseline)
        self.assertEqual(result["verdict"], "phase_sensitive")

    def test_flat_confusion_is_reported_as_phase_blind(self) -> None:
        """When shifted content is indistinguishable from aligned content the
        evaluator must say so instead of silently adjudicating."""
        reference = _gray_frame(8, 8, 40)
        other_phase = _gray_frame(8, 8, 40)  # static content: no phase cue
        baseline = _gray_frame(8, 8, 41)
        result = phase_sanity([reference, other_phase], baseline)
        self.assertEqual(result["verdict"], "phase_blind")


class AdjudicationTests(unittest.TestCase):
    def test_better_fidelity_with_reference_bounded_detail_is_accepted(self) -> None:
        # Reference has mild texture; candidate approaches it better than the
        # baseline without exceeding the reference's detail energy.
        reference = _checker_frame(16, 16, 100, 6)
        baseline = _gray_frame(16, 16, 100)
        candidate = _checker_frame(16, 16, 100, 4)
        result = adjudicate(reference, baseline, candidate, stride=1)
        self.assertEqual(result["verdict"], "accept", result["reasons"])

    def test_unsupported_detail_is_rejected_even_when_fidelity_improves(self) -> None:
        reference = _checker_frame(16, 16, 100, 2)
        baseline = _gray_frame(16, 16, 100)
        # Candidate averages closer to the reference mean but injects a
        # strong high-frequency pattern the reference does not have.
        candidate = _checker_frame(16, 16, 100, 40)
        result = adjudicate(reference, baseline, candidate, stride=1)
        self.assertEqual(result["verdict"], "reject")
        self.assertTrue(result["rule_outcomes"]["unsupported_detail"])
        self.assertIn("unsupported detail", " ".join(result["reasons"]))

    def test_sharpening_only_gain_is_rejected(self) -> None:
        """Candidate fidelity improves (2.0 vs 6.0 MAE) but only via
        overshoot-driven detail beyond the reference: a sharpening-only
        gain, not reconstruction."""
        reference = _checker_frame(16, 16, 100, 6)
        baseline = _gray_frame(16, 16, 100)
        candidate = _checker_frame(16, 16, 100, 8)
        result = adjudicate(reference, baseline, candidate, stride=1)
        self.assertEqual(result["verdict"], "reject")
        self.assertTrue(result["rule_outcomes"]["fidelity_win"])
        self.assertTrue(result["rule_outcomes"]["sharpening_only"])
        self.assertIn("sharpening-only", " ".join(result["reasons"]))

    def test_worse_fidelity_is_rejected_regardless_of_detail(self) -> None:
        reference = _gray_frame(16, 16, 100)
        baseline = _gray_frame(16, 16, 102)
        candidate = _gray_frame(16, 16, 120)
        result = adjudicate(reference, baseline, candidate, stride=1)
        self.assertEqual(result["verdict"], "reject")
        self.assertFalse(result["rule_outcomes"]["fidelity_win"])

    def test_rejection_reasons_are_explicit_for_audit(self) -> None:
        reference = _checker_frame(16, 16, 100, 2)
        baseline = _gray_frame(16, 16, 100)
        candidate = _checker_frame(16, 16, 100, 40)
        result = adjudicate(reference, baseline, candidate, stride=1)
        self.assertTrue(result["reasons"])
        self.assertIn("stride", result["metrics"])


if __name__ == "__main__":
    unittest.main()
