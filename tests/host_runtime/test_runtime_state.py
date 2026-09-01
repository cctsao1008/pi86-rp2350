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
    WorkloadRuntimeState,
)
from rp86_runtime.workload import (  # noqa: E402
    PROCESSOR_FLAG_IDLE,
    PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
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
            "<IIIIII",
            9,
            5,
            623,
            2,
            3748,
            PROCESSOR_FLAG_IDLE,
        )
        state = WorkloadRuntimeState.from_payload(payload)
        self.assertEqual(state.workload_id, 9)
        self.assertEqual(state.lifecycle_name, "COMPLETED")
        self.assertEqual(state.clock_name, "CLOCK-STEPPED")
        self.assertEqual(state.processor_state, "IDLE / HLT")
        self.assertTrue(state.completed)
        self.assertTrue(state.upload_requires_stop)

    def test_prepared_runtime_requires_empty_active_runtime(self) -> None:
        state = WorkloadRuntimeState(
            lifecycle=0,
            clock_mode=1,
            processor_flags=PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
        )
        self.assertTrue(state.prepared_runtime_available)
        state.lifecycle = 3
        self.assertFalse(state.prepared_runtime_available)


if __name__ == "__main__":
    unittest.main()
