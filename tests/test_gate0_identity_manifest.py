"""Gate-0 identity manifest contract tests.

These tests lock the identity-spine semantics before any experiment is allowed
to depend on it: SHA-256 coverage, environment capture, and — most important —
the fail-closed runtime-trace cross-check, where omitted provenance must fail
instead of silently passing.
"""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from benchmarks.gate0.experiment_identity import (
    IdentityError,
    binary_identity,
    configuration_identity,
    runtime_trace_crosscheck,
    sha256_bytes,
    sha256_file,
)


class Sha256Tests(unittest.TestCase):
    def test_sha256_file_matches_reference_hash(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "blob.bin"
            payload = b"temporal forge gate0 identity payload"
            path.write_bytes(payload)
            self.assertEqual(sha256_file(path), hashlib.sha256(payload).hexdigest())
            self.assertEqual(
                sha256_bytes(payload), hashlib.sha256(payload).hexdigest()
            )


class BinaryIdentityTests(unittest.TestCase):
    def test_missing_binary_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(IdentityError):
                binary_identity(Path(tmp) / "absent_player")


class ConfigurationIdentityTests(unittest.TestCase):
    def test_captures_only_tforge_namespace_sorted(self) -> None:
        env = {
            "PATH": "/usr/bin",
            "TFORGE_Z_LAST": "1",
            "TFORGE_A_FIRST": "2",
            "TFORGE_FSR4_JITTER_MODE": "off",
        }
        identity = configuration_identity(environment=env)
        self.assertEqual(
            list(identity["tforge_env"].keys()),
            ["TFORGE_A_FIRST", "TFORGE_FSR4_JITTER_MODE", "TFORGE_Z_LAST"],
        )
        self.assertEqual(identity["tforge_env"]["TFORGE_A_FIRST"], "2")
        self.assertIsNone(identity["quality_lab_config_sha256"])

    def test_quality_lab_config_hash_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp) / "quality_lab.json"
            config.write_text('{"enabled": true}', encoding="utf-8")
            identity = configuration_identity(
                environment={}, quality_lab_config=config
            )
            self.assertEqual(
                identity["quality_lab_config_sha256"],
                hashlib.sha256(b'{"enabled": true}').hexdigest(),
            )

    def test_missing_quality_lab_config_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(IdentityError):
                configuration_identity(
                    environment={}, quality_lab_config=Path(tmp) / "absent.json"
                )


def _write_trace(path: Path, **overrides: str) -> Path:
    trace = {
        "schema": "temporal_forge.runtime_pipeline.v1",
        "binary_sha256": overrides.get("binary_sha256", "b" * 64),
        "git_head": overrides.get("git_head", "c" * 40),
        "config_sha256": overrides.get("config_sha256", ""),
        "run_id": overrides.get("run_id", "FFW-T0-TEST"),
    }
    path.write_text(json.dumps(trace), encoding="utf-8")
    return path


def _manifest(binary_sha: str, git_head: str, config_sha: str | None) -> dict:
    return {
        "binary": {"sha256": binary_sha},
        "git": {"head": git_head},
        "configuration": {"quality_lab_config_sha256": config_sha},
        "run_id": "FFW-T0-TEST",
    }


class RuntimeTraceCrosscheckTests(unittest.TestCase):
    def test_matching_trace_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = _write_trace(
                Path(tmp) / "trace.json",
                binary_sha256="b" * 64,
                git_head="c" * 40,
            )
            result = runtime_trace_crosscheck(
                trace, _manifest("b" * 64, "c" * 40, None)
            )
            self.assertTrue(result["passed"])
            self.assertTrue(result["trace_schema_known"])

    def test_empty_binary_hash_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = _write_trace(Path(tmp) / "trace.json", binary_sha256="")
            result = runtime_trace_crosscheck(
                trace, _manifest("b" * 64, "c" * 40, None)
            )
            self.assertFalse(result["passed"])

    def test_mismatched_binary_hash_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = _write_trace(Path(tmp) / "trace.json", binary_sha256="d" * 64)
            result = runtime_trace_crosscheck(
                trace, _manifest("b" * 64, "c" * 40, None)
            )
            self.assertFalse(result["passed"])

    def test_empty_git_head_is_unrecorded_provenance_and_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = _write_trace(Path(tmp) / "trace.json", git_head="")
            result = runtime_trace_crosscheck(
                trace, _manifest("b" * 64, "c" * 40, None)
            )
            self.assertFalse(result["passed"])

    def test_current_checkout_is_never_substituted_for_trace_commit(self) -> None:
        """An absent trace must raise, not be repaired from the local repo."""
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(IdentityError):
                runtime_trace_crosscheck(
                    Path(tmp) / "absent.json",
                    _manifest("b" * 64, "c" * 40, None),
                )

    def test_config_hash_empty_on_both_sides_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = _write_trace(Path(tmp) / "trace.json", config_sha256="")
            result = runtime_trace_crosscheck(
                trace, _manifest("b" * 64, "c" * 40, None)
            )
            self.assertTrue(result["passed"])

    def test_config_hash_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = _write_trace(
                Path(tmp) / "trace.json", config_sha256="e" * 64
            )
            result = runtime_trace_crosscheck(
                trace, _manifest("b" * 64, "c" * 40, "f" * 64)
            )
            self.assertFalse(result["passed"])

    def test_unknown_trace_schema_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace_path = Path(tmp) / "trace.json"
            _write_trace(trace_path)
            payload = json.loads(trace_path.read_text(encoding="utf-8"))
            payload["schema"] = "something.else.v9"
            trace_path.write_text(json.dumps(payload), encoding="utf-8")
            result = runtime_trace_crosscheck(
                trace_path, _manifest("b" * 64, "c" * 40, None)
            )
            self.assertFalse(result["trace_schema_known"])


if __name__ == "__main__":
    unittest.main()
