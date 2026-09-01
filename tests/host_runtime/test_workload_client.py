from pathlib import Path
import struct
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.protocol import Message, TYPE_WORKLOAD_STATUS  # noqa: E402
from rp86_runtime.runtime_state import (  # noqa: E402
    RequestSequence,
    WorkloadRuntimeState,
)
from rp86_runtime.workload_client import WorkloadClient  # noqa: E402


class WorkloadClientTests(unittest.TestCase):
    def test_success_updates_state_sequence_and_callbacks(self) -> None:
        sequence = RequestSequence(4)
        state = WorkloadRuntimeState()
        updates: list[bool] = []
        activity: list[bool] = []
        request = Message(TYPE_WORKLOAD_STATUS, 4)
        reply = Message(
            TYPE_WORKLOAD_STATUS,
            4,
            struct.pack("<IIIIII", 3, 2, 623, 2, 100, 0),
        )
        client = WorkloadClient(
            lambda _request: (reply, 2.5, None),
            sequence,
            state,
            lambda: updates.append(True),
            lambda: activity.append(True),
        )

        result = client.transact([request])

        self.assertTrue(result.success)
        self.assertEqual(result.latencies_ms, (2.5,))
        self.assertEqual(sequence.value, 5)
        self.assertEqual(state.workload_id, 3)
        self.assertEqual(state.lifecycle_name, "STAGED")
        self.assertEqual(updates, [True])
        self.assertEqual(activity, [True])

    def test_transport_failure_does_not_consume_sequence(self) -> None:
        sequence = RequestSequence(8)
        client = WorkloadClient(
            lambda _request: (None, 1.0, "transport failed"),
            sequence,
            WorkloadRuntimeState(),
            lambda: None,
            lambda: None,
        )

        result = client.transact([Message(TYPE_WORKLOAD_STATUS, 8)])

        self.assertFalse(result.success)
        self.assertEqual(result.failed_index, 1)
        self.assertEqual(sequence.value, 8)


if __name__ == "__main__":
    unittest.main()
