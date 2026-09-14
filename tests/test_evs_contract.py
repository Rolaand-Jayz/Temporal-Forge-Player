import unittest

from benchmarks.empirical.evs_evaluate import select_strongest_spatial_baseline
from benchmarks.empirical.evs_manifest import METHODS, REGIMES, SEQUENCES, STRUCTURAL_CLASSES, build_manifest, matrix


class EmpiricalViabilityContractTests(unittest.TestCase):
    def test_primary_matrix_is_exact_cartesian_product(self):
        cells = matrix()
        self.assertEqual(len(cells), 64)
        self.assertEqual(len({cell["cell_id"] for cell in cells}), 64)
        self.assertEqual({cell["structural_class"] for cell in cells}, set(STRUCTURAL_CLASSES))
        self.assertEqual({cell["regime"] for cell in cells}, set(REGIMES))
        self.assertEqual({cell["method"] for cell in cells}, set(METHODS))
        self.assertEqual({cell["sequence"] for cell in cells}, set(SEQUENCES))

    def test_frozen_methods_have_complete_runtime_contracts(self):
        manifest = build_manifest()
        self.assertEqual(manifest["limits"]["primary_cells"], 64)
        self.assertEqual(manifest["fixture"]["hr_dimensions"], [1920, 1080])
        self.assertEqual(manifest["fixture"]["lr_dimensions"], [640, 360])
        self.assertEqual(manifest["fixture"]["frame_count"], 8)
        for method_id in ("M2", "M3", "M4"):
            method = manifest["methods"][method_id]
            self.assertTrue(method["config"]["sha256"])
            self.assertIn("TFORGE_FSR4_FORCE_VIEWPORT", method["environment"])
            self.assertIn("TFORGE_FSR4_DISABLE_CAS", method["environment"])

    def test_m1_is_external_and_m2_is_recorded_spatial_control(self):
        manifest = build_manifest()
        self.assertEqual(manifest["methods"]["M1"]["kind"], "external_ffmpeg_spatial")
        self.assertIn("base_only_bilinear", manifest["methods"]["M2"]["config"]["path"])

    def test_baseline_selection_uses_measured_spatial_dominance(self):
        rows = [
            {
                "method": "M1",
                "mean_psnr_db": 30.0,
                "mean_ssim": 0.90,
                "mean_hf_correlation": 0.60,
                "mean_registered_temporal_error": 0.004,
            },
            {
                "method": "M2",
                "mean_psnr_db": 28.0,
                "mean_ssim": 0.88,
                "mean_hf_correlation": 0.55,
                "mean_registered_temporal_error": 0.005,
            },
        ]
        selected, evidence = select_strongest_spatial_baseline(
            rows,
            method_specs={
                "M1": {"kind": "external_ffmpeg_spatial"},
                "M2": {"kind": "player_quality_lab_spatial"},
            },
        )
        self.assertEqual(selected, "M1")
        self.assertEqual(evidence["selected_method"], "M1")

    def test_baseline_selection_does_not_hard_code_m1_or_m2(self):
        rows = [
            {
                "method": "M1",
                "mean_psnr_db": 28.0,
                "mean_ssim": 0.88,
                "mean_hf_correlation": 0.55,
                "mean_registered_temporal_error": 0.005,
            },
            {
                "method": "M2",
                "mean_psnr_db": 30.0,
                "mean_ssim": 0.90,
                "mean_hf_correlation": 0.60,
                "mean_registered_temporal_error": 0.004,
            },
        ]
        selected, _evidence = select_strongest_spatial_baseline(
            rows,
            method_specs={
                "M1": {"kind": "external_ffmpeg_spatial"},
                "M2": {"kind": "player_quality_lab_spatial"},
            },
        )
        self.assertEqual(selected, "M2")


if __name__ == "__main__":
    unittest.main()
