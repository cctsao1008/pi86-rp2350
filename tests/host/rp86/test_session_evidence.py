from datetime import datetime, timezone
import json
from pathlib import Path
import tempfile
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.session_evidence import SessionEvidence, regression_failure_reasons  # noqa: E402
from rp86_runtime.runtime_state import WorkloadRuntimeState  # noqa: E402
from rp86_runtime.workload import WorkloadManifest  # noqa: E402


class SessionEvidenceTests(unittest.TestCase):
    def evidence(self):
        return SessionEvidence(Path("unused.log"), Path("unused.json"))

    def completed(self):
        return WorkloadRuntimeState(
            workload_id=1, lifecycle=5, cycles=3212, processor_flags=1,
            result_flags=11, completion_reason=2, processor_signature=0x12,
            native_output=b"RESULT: PASS", structured_result=True,
        )

    def test_complete_result_and_image_round_trip(self):
        evidence = self.evidence()
        manifest = WorkloadManifest.for_image(
            b"\x90\xf4", load_address=0x10000, entry_segment=0x1000, entry_offset=0,
        )
        evidence.bind_workload(1, "flash:/INVSQRT.P86W", manifest)
        state = self.completed()
        evidence.observe_workload(state)
        result = json.loads(json.dumps(evidence.workload_results[0]))
        self.assertEqual(result["image"]["name"], "INVSQRT.P86W")
        self.assertEqual(result["image"]["image_crc32"], manifest.image_crc32)
        self.assertEqual(result["image"]["load_address"], 0x10000)
        self.assertEqual(result["image"]["entry_segment"], 0x1000)
        self.assertEqual(result["processor_identity"]["signature"], 0x12)
        self.assertEqual(result["processor_identity"]["source"], "firmware_boot_aad16")
        self.assertFalse(result["processor_identity"]["fresh_liveness_proof"])
        self.assertEqual(result["cycles"], 3212)
        self.assertEqual(result["completion_reason"], "NATIVE_HLT")
        self.assertEqual(bytes.fromhex(result["native_output"]["hex"]), b"RESULT: PASS")
        self.assertEqual(result["outcome"], "PASS")
        self.assertEqual(regression_failure_reasons(state), [])

    def test_terminal_poll_dedup_and_restart_history(self):
        evidence = self.evidence()
        state = self.completed()
        evidence.observe_workload(state)
        evidence.observe_workload(state)
        self.assertEqual(len(evidence.workload_results), 1)
        state.lifecycle = 3
        evidence.observe_workload(state)
        state.lifecycle = 5
        evidence.observe_workload(state)
        self.assertEqual(len(evidence.workload_results), 2)

    def test_unknown_attached_image_does_not_inherit_previous_metadata(self):
        evidence = self.evidence()
        manifest = WorkloadManifest.for_image(
            b"\xf4", load_address=0x10000, entry_segment=0x1000, entry_offset=0,
        )
        evidence.bind_workload(1, r"C:\work\OLD.P86W", manifest)
        state = self.completed()
        state.workload_id = 2
        evidence.observe_workload(state)
        self.assertIsNone(evidence.workload_results[-1]["image"])
        state.workload_id = 1
        evidence.observe_workload(state)
        self.assertIsNone(evidence.workload_results[-1]["image"])

    def test_raw_bytes_and_truncation_are_not_display_annotations(self):
        state = self.completed()
        state.native_output = b"\xff\x00A\n"
        state.result_flags |= 4
        result = self.evidence().workload_snapshot(state)
        self.assertEqual(result["native_output"]["hex"], "ff00410a")
        self.assertEqual(result["native_output"]["byte_length"], 4)
        self.assertEqual(result["native_output"]["text"], "\ufffd\x00A\n")
        self.assertTrue(result["native_output"]["truncated"])
        self.assertIn("native output is not exact RESULT: PASS", regression_failure_reasons(state))

    def test_incomplete_and_failure_outcomes_are_explicit(self):
        evidence = self.evidence()
        state = self.completed()
        for lifecycle in (0, 2, 3, 4):
            state.lifecycle = lifecycle
            result = evidence.workload_snapshot(state)
            self.assertIsNone(result["passed"])
            self.assertEqual(result["outcome"], "INCOMPLETE")
        state.lifecycle = 7
        state.completion_reason = 3
        result = evidence.workload_snapshot(state)
        self.assertFalse(result["passed"])
        self.assertIsNotNone(result["failure_reason"])
        state.lifecycle = 5
        state.result_flags = 0
        result = evidence.workload_snapshot(state)
        self.assertFalse(result["passed"])
        self.assertEqual(result["failure_reason"], "native PASS flag absent")

    def test_writes_raw_transport_and_structured_session(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            evidence = SessionEvidence.create(
                Path(directory),
                datetime(2026, 9, 1, 3, 30, tzinfo=timezone.utc),
            )
            evidence.capture(b"physical evidence\r\n")
            evidence.record({"sequence": 7, "passed": True})
            evidence.failure("load", "CRC mismatch", source="BAD.P86W")
            evidence.observe_workload(self.completed())

            evidence.write({"schema": "rp86.runtime-session/v1"})

            self.assertEqual(
                evidence.raw_path.read_bytes(), b"physical evidence\r\n"
            )
            document = json.loads(evidence.json_path.read_text(encoding="utf-8"))
            self.assertEqual(document["events"], [{"sequence": 7, "passed": True}])
            self.assertEqual(document["errors"][0]["reason"], "CRC mismatch")
            self.assertEqual(document["workload_results"][0]["cycles"], 3212)
            self.assertEqual(
                Path(document["raw_cdc_log"]), evidence.raw_path.resolve()
            )


if __name__ == "__main__":
    unittest.main()
