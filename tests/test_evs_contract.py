import unittest

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


if __name__ == "__main__":
    unittest.main()
