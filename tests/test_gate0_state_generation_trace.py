"""Gate-0 R3 contract: per-frame state-generation tracing in the event trace.

The event trace must bind every dumped frame to the seek generation, reset
generations, uploader resource generation, motion-producer identity, and
dispatch geometry it was produced under, so stale or mismatched temporal
state is detectable from a capture without operator knowledge.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAYBACK = ROOT / "src/core/PlaybackEngine.cpp"
UPLOADER_HPP = ROOT / "src/render/GpuImageUploader.hpp"
UPLOADER_CPP = ROOT / "src/render/GpuImageUploader.cpp"


class StateGenerationTraceContractTests(unittest.TestCase):
    def test_event_trace_declares_state_generation_section(self) -> None:
        source = PLAYBACK.read_text(encoding="utf-8")
        for marker in (
            "stateGenerations",
            "seekGeneration",
            "sceneCutCount",
            "historyResetCount",
            "temporalResetCount",
            "uploaderAllocationGeneration",
            "motionProducer",
            "resourceGeometry",
            "modelWidth",
            "outputWidth",
        ):
            self.assertIn(marker, source, marker)

    def test_temporal_reset_generation_advances_only_on_commit(self) -> None:
        """The reset generation must advance inside the successful-dispatch
        branch (after temporalFrameContinuity.commit), never on failure."""
        source = PLAYBACK.read_text(encoding="utf-8")
        commit_index = source.index("temporalFrameContinuity.commit(sourceFrameIndex)")
        counter = source.index("temporalResetCount_.fetch_add")
        self.assertGreater(
            counter,
            commit_index,
            "reset generation increment must follow the continuity commit",
        )
        self.assertIn("if (in.reset)", source[counter - 200 : counter])

    def test_uploader_exposes_allocation_generation(self) -> None:
        header = UPLOADER_HPP.read_text(encoding="utf-8")
        impl = UPLOADER_CPP.read_text(encoding="utf-8")
        self.assertIn("allocationGeneration", header)
        self.assertIn("allocationGeneration_", header)
        self.assertIn("++allocationGeneration_", impl)
        no_change_guard = impl.index("return true; // no change")
        increment = impl.index("++allocationGeneration_")
        self.assertGreater(
            increment,
            no_change_guard,
            "allocation generation must advance only on real (re)allocation",
        )

    def test_producer_label_records_ablation_and_dense_replay(self) -> None:
        source = PLAYBACK.read_text(encoding="utf-8")
        self.assertIn("fsr4MotionProducerLabel_", source)
        self.assertIn('+ablation:', source)
        self.assertIn("+dense_replay", source)

    def test_runtime_trace_reports_effective_default_jitter_as_off(self) -> None:
        """Gate-0 R4 defect fix: unset/unknown jitter mode is Off in behavior,
        so the runtime trace must not report jitter_enabled=true for it."""
        source = PLAYBACK.read_text(encoding="utf-8")
        writer = source[source.index("writeRuntimePipelineTrace"):]
        self.assertIn("effectiveJitterMode", writer)
        self.assertIn('effectiveJitterMode = "off"', writer)
        self.assertIn('"requested_jitter_mode"', writer)

    def test_event_trace_call_site_passes_generation_values(self) -> None:
        source = PLAYBACK.read_text(encoding="utf-8")
        call_index = source.rindex("dumpEventTraceFrame(")
        call_block = source[call_index : call_index + 900]
        for fragment in (
            "seekGeneration_.load",
            "sceneCuts_.load",
            "historyResets_.load",
            "temporalResetCount_.load",
            "allocationGeneration()",
            "fsr4MotionProducerLabel_",
            "fsrModelW",
            "jitterPair.neuralTargetW",
        ):
            self.assertIn(fragment, call_block, fragment)


if __name__ == "__main__":
    unittest.main()
