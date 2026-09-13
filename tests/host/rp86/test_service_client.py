from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from host.rp86.filesystem import list_request  # noqa: E402
from host.rp86.runtime_state import RequestSequence  # noqa: E402
from host.rp86.service_client import RuntimeServiceClient  # noqa: E402


class RuntimeServiceClientTests(unittest.TestCase):
    def test_failed_request_still_advances_sequence_and_defers_probe(self) -> None:
        sequence = RequestSequence(5)
        activity: list[bool] = []
        client = RuntimeServiceClient(
            lambda _request: (None, 1.0, "transport failed"),
            sequence,
            lambda: activity.append(True),
        )

        reply, error = client.filesystem_request(
            list_request("flash:/", 0, sequence.value)
        )

        self.assertIsNone(reply)
        self.assertEqual(error, "transport failed")
        self.assertEqual(sequence.value, 6)
        self.assertEqual(activity, [True])


if __name__ == "__main__":
    unittest.main()
