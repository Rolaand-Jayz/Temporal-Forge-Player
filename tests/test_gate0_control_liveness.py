"""Gate-0 R4 contract: control-liveness verdict rules.

The verifier is a pure function so these tests lock the classification
semantics without a player: trace visibility on both arms, output-difference
handling, and the fail-closed treatment of failed or unproven arms.
"""

from __future__ import annotations

import unittest

from benchmarks.gate0.control_liveness import Control, TraceCheck, verify_control


def _record(ok: bool = True, dumps: dict | None = None,
            log_text: str = "", trace: dict | None = None,
            arm: str = "default") -> dict:
    return {
        "ok": ok,
        "dumps": dumps or {},
        "player_log": f"/tmp/fake-gate0-{arm}.log",  # replaced in tests that check logs
        "_log_text": log_text,
        "runtime_trace": trace or {},
        "output_dir": f"/tmp/fake-gate0-{arm}-out",
        "runtime_trace_path": f"/tmp/fake-gate0-{arm}-trace.json",
    }


class _FakePaths(unittest.TestCase):
    """Patches path-backed checks with in-memory content."""

    def _verify(self, control, default, set_rec):
        from unittest import mock
        import benchmarks.gate0.control_liveness as cl

        for rec, arm in ((default, "default"), (set_rec, "set")):
            rec["player_log"] = f"/tmp/fake-gate0-{arm}.log"
            rec["runtime_trace_path"] = f"/tmp/fake-gate0-{arm}-trace.json"
            rec["output_dir"] = f"/tmp/fake-gate0-{arm}-out"

        real_read_text = cl.Path.read_text

        def fake_read_text(self, *args, **kwargs):
            for rec in (default, set_rec):
                if str(self) == rec["player_log"]:
                    return rec["_log_text"]
                if str(self) == rec["runtime_trace_path"]:
                    import json
                    return json.dumps(rec["runtime_trace"])
            return real_read_text(self, *args, **kwargs)

        real_is_file = cl.Path.is_file

        def fake_is_file(self):
            for rec in (default, set_rec):
                if str(self) in (rec["player_log"], rec["runtime_trace_path"]):
                    return True
            return real_is_file(self)

        with mock.patch.object(cl.Path, "read_text", fake_read_text), \
                mock.patch.object(cl.Path, "is_file", fake_is_file), \
                mock.patch.object(cl.Path, "glob", lambda self, pat: []):
            return verify_control(control, default, set_rec)


class VerifyControlTests(_FakePaths):
    def test_trace_visible_and_outputs_differ_is_live_output(self) -> None:
        control = Control(
            "c", {"K": "V"},
            checks=[
                TraceCheck("set", "runtime_trace_field", "flag", True),
                TraceCheck("default", "runtime_trace_field", "flag", False),
            ],
        )
        default = _record(trace={"flag": False}, dumps={"f0.ppm": "aaa"})
        set_rec = _record(trace={"flag": True}, dumps={"f0.ppm": "bbb"})
        result = self._verify(control, default, set_rec)
        self.assertEqual(result["verdict"], "live_output")
        self.assertTrue(result["output_differs"])

    def test_trace_visible_but_identical_outputs_is_live_trace_only(self) -> None:
        control = Control(
            "c", {"K": "V"},
            checks=[TraceCheck("set", "runtime_trace_field", "flag", True)],
        )
        default = _record(trace={"flag": False}, dumps={"f0.ppm": "same"})
        set_rec = _record(trace={"flag": True}, dumps={"f0.ppm": "same"})
        result = self._verify(control, default, set_rec)
        self.assertEqual(result["verdict"], "live_trace_only")

    def test_failed_trace_check_is_no_op(self) -> None:
        control = Control(
            "c", {"K": "V"},
            checks=[TraceCheck("set", "runtime_trace_field", "flag", True)],
        )
        default = _record(trace={"flag": False}, dumps={"f0.ppm": "a"})
        set_rec = _record(trace={"flag": False}, dumps={"f0.ppm": "b"})
        result = self._verify(control, default, set_rec)
        self.assertEqual(result["verdict"], "no_op")

    def test_failed_arm_is_inconclusive_never_live(self) -> None:
        control = Control(
            "c", {"K": "V"},
            checks=[TraceCheck("set", "runtime_trace_field", "flag", True)],
        )
        default = _record(ok=False, trace={"flag": False})
        set_rec = _record(trace={"flag": True}, dumps={"f0.ppm": "b"})
        result = self._verify(control, default, set_rec)
        self.assertEqual(result["verdict"], "inconclusive")

    def test_log_check_requires_literal_substring_in_set_arm(self) -> None:
        control = Control(
            "c", {"K": "V"},
            checks=[TraceCheck("set", "log_contains", "", "threshold=0.35")],
        )
        default = _record(log_text="threshold=0.55", dumps={"f0.ppm": "same"})
        set_rec = _record(log_text="... historyGatePass=true threshold=0.35",
                          dumps={"f0.ppm": "same"})
        result = self._verify(control, default, set_rec)
        self.assertEqual(result["verdict"], "live_trace_only")
        self.assertTrue(result["checks"][0]["passed"])

    def test_no_disjoint_dumps_is_inconclusive(self) -> None:
        control = Control(
            "c", {"K": "V"},
            checks=[TraceCheck("set", "runtime_trace_field", "flag", True)],
        )
        default = _record(trace={"flag": False}, dumps={"f0.ppm": "a"})
        set_rec = _record(trace={"flag": True}, dumps={"f1.ppm": "b"})
        result = self._verify(control, default, set_rec)
        self.assertEqual(result["verdict"], "inconclusive")


if __name__ == "__main__":
    unittest.main()
