from pathlib import Path
import struct
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.protocol import (  # noqa: E402
    NATIVE_PROCESSOR_INTEL_8086,
    NativeServiceWitness,
    TYPE_HEARTBEAT,
)
from rp86_runtime.runtime_state import (  # noqa: E402
    ProcessorObservationState,
    RequestSequence,
    RuntimeStatusSnapshot,
    WorkloadRuntimeState,
)
from rp86_runtime.workload import (  # noqa: E402
    PROCESSOR_FLAG_IDLE,
    PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
    RESULT_FLAG_NATIVE_OUTPUT,
    RESULT_FLAG_PASS,
    RESULT_FLAG_PROCESSOR_IDENTIFIED,
)


class RuntimeStateTests(unittest.TestCase):
    def test_processor_observation_accepts_one_coherent_witness(self) -> None:
        state = ProcessorObservationState()
        witness = NativeServiceWitness(
            service_type=TYPE_HEARTBEAT,
            boot_id=7,
            cpu_sequence=123,
            command_sequence=9,
            text=b"",
            flags=NATIVE_PROCESSOR_INTEL_8086,
        )

        state.accept_witness(witness)

        self.assertEqual(state.processor, "intel-8086")
        self.assertEqual(state.boot_id, 7)
        self.assertEqual(state.cpu_sequence, 123)
        self.assertEqual(state.command_sequence, 9)
        self.assertTrue(state.connected)

    def test_request_sequence_wraps_without_using_zero(self) -> None:
        sequence = RequestSequence(0)
        self.assertEqual(sequence.value, 1)
        self.assertEqual(sequence.advance_after(0xFFFFFFFF), 1)

    def test_status_payload_updates_one_coherent_state(self) -> None:
        payload = struct.pack(
            "<IIIIIIIIHH16s",
            9,
            5,
            623,
            2,
            3748,
            PROCESSOR_FLAG_IDLE,
            RESULT_FLAG_PASS | RESULT_FLAG_NATIVE_OUTPUT |
            RESULT_FLAG_PROCESSOR_IDENTIFIED,
            2,
            0x12,
            12,
            b"RESULT: PASS".ljust(16, b"\0"),
        )
        state = WorkloadRuntimeState.from_payload(payload)
        self.assertEqual(state.workload_id, 9)
        self.assertEqual(state.lifecycle_name, "COMPLETED")
        self.assertEqual(state.clock_name, "CLOCK-STEPPED")
        self.assertEqual(state.processor_state, "IDLE / HLT")
        self.assertTrue(state.completed)
        self.assertTrue(state.upload_requires_stop)
        self.assertTrue(state.passed)
        self.assertTrue(state.processor_identified)
        self.assertTrue(state.physical_regression_passed)
        self.assertEqual(state.completion_reason_name, "NATIVE_HLT")
        self.assertEqual(state.processor_signature, 0x12)
        self.assertEqual(state.native_output_text, "RESULT: PASS")

    def test_processor_identity_requires_flag_and_known_signature(self) -> None:
        state = WorkloadRuntimeState(processor_signature=0x12)
        self.assertFalse(state.processor_identified)
        state.result_flags = RESULT_FLAG_PROCESSOR_IDENTIFIED
        self.assertTrue(state.processor_identified)
        state.processor_signature = 0xFFFF
        self.assertFalse(state.processor_identified)
        self.assertFalse(state.physical_regression_passed)

    def test_prepared_runtime_requires_empty_active_runtime(self) -> None:
        state = WorkloadRuntimeState(
            lifecycle=0,
            clock_mode=1,
            processor_flags=PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
        )
        self.assertTrue(state.prepared_runtime_available)
        state.lifecycle = 3
        self.assertFalse(state.prepared_runtime_available)

    def test_runtime_snapshot_publishes_structured_result(self) -> None:
        workload = WorkloadRuntimeState(
            workload_id=4,
            lifecycle=5,
            cycles=3212,
            result_flags=RESULT_FLAG_PASS | RESULT_FLAG_NATIVE_OUTPUT,
            completion_reason=2,
            processor_signature=0x12,
            native_output=b"RESULT: PASS",
            structured_result=True,
        )
        snapshot = RuntimeStatusSnapshot(
            transport_state="OWNER_ACTIVE",
            processor_mode="auto",
            request_sequence=9,
            observation=ProcessorObservationState(processor="intel-8086"),
            workload=workload,
        ).as_dict()
        self.assertTrue(snapshot["workload_result_pass"])
        self.assertEqual(snapshot["workload_completion_reason"], "NATIVE_HLT")
        self.assertEqual(snapshot["workload_native_output"], "RESULT: PASS")


if __name__ == "__main__":
    unittest.main()
