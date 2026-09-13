"""Gate-0 R2 contract: determinism probe verifier and PPM metric semantics.

Locked without a player: PPM parsing (including rejection of malformed
payloads), pairwise MAE, and the verdict classification — byte-identical,
metric-stable within the threshold, or nondeterministic beyond it.
"""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from benchmarks.gate0.determinism_probe import (
    MAE_THRESHOLD,
    ppm_mae,
    read_ppm,
    verify_determinism,
)


def _ppm_bytes(width: int, height: int, value: int) -> bytes:
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    return header + bytes([value]) * (width * height * 3)


class PpmTests(unittest.TestCase):
    def test_roundtrip_parse(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "f.ppm"
            path.write_bytes(_ppm_bytes(4, 3, 7))
            width, height, payload = read_ppm(path)
            self.assertEqual((width, height), (4, 3))
            self.assertEqual(len(payload), 4 * 3 * 3)
            self.assertEqual(set(payload), {7})

    def test_identical_images_have_zero_mae(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.ppm"
            b = Path(tmp) / "b.ppm"
            a.write_bytes(_ppm_bytes(2, 2, 10))
            b.write_bytes(_ppm_bytes(2, 2, 10))
            self.assertEqual(ppm_mae(a, b), 0.0)

    def test_shifted_images_have_expected_mae(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.ppm"
            b = Path(tmp) / "b.ppm"
            a.write_bytes(_ppm_bytes(1, 1, 0))
            b.write_bytes(_ppm_bytes(1, 1, 10))
            self.assertAlmostEqual(ppm_mae(a, b), 10.0)

    def test_dimension_mismatch_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.ppm"
            b = Path(tmp) / "b.ppm"
            a.write_bytes(_ppm_bytes(2, 2, 0))
            b.write_bytes(_ppm_bytes(3, 2, 0))
            with self.assertRaises(ValueError):
                ppm_mae(a, b)

    def test_short_payload_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.ppm"
            path.write_bytes(_ppm_bytes(4, 3, 7)[:-5])
            with self.assertRaises(ValueError):
                read_ppm(path)


def _run(index: int, ok: bool = True, dumps: dict | None = None) -> dict:
    return {
        "run_id": f"FFW-T0-2-det-{index:02d}",
        "ok": ok,
        "dumps": dumps or {},
        "dump_dir": f"run_{index:02d}/dumps",
    }


class VerifyDeterminismTests(unittest.TestCase):
    def test_identical_hashes_are_byte_identical(self) -> None:
        runs = [_run(0, dumps={"f0.ppm": "h"}),
                _run(1, dumps={"f0.ppm": "h"}),
                _run(2, dumps={"f0.ppm": "h"})]
        result = verify_determinism(runs, Path("/tmp"))
        self.assertEqual(result["verdict"], "byte_identical")

    def test_fewer_than_two_completed_runs_is_inconclusive(self) -> None:
        runs = [_run(0, ok=False, dumps={"f0.ppm": "h"}),
                _run(1, dumps={"f0.ppm": "h"})]
        result = verify_determinism(runs, Path("/tmp"))
        self.assertEqual(result["verdict"], "inconclusive")

    def test_hash_mismatch_without_dump_files_is_nondeterministic(self) -> None:
        """A hash mismatch needs a payload comparison; missing payloads must
        fail closed as nondeterministic, never silently pass."""
        runs = [_run(0, dumps={"f0.ppm": "h1"}),
                _run(1, dumps={"f0.ppm": "h2"})]
        result = verify_determinism(runs, Path("/tmp"))
        self.assertEqual(result["verdict"], "nondeterministic")


if __name__ == "__main__":
    unittest.main()
