from pathlib import Path
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.protocol import Message, TYPE_WORKLOAD_CONTROL, TYPE_WORKLOAD_STATUS  # noqa: E402
from rp86_runtime.request_channel import exchange_hid_request  # noqa: E402
from rp86_runtime.transport import hid_output_report  # noqa: E402


class FakeHid:
    def __init__(self, replies: list[bytes]) -> None:
        self.replies = list(replies)
        self.writes: list[bytes] = []

    def read(self, _length: int) -> bytes:
        return self.replies.pop(0) if self.replies else b""

    def write(self, record: bytes) -> int:
        self.writes.append(record)
        return len(record)


class RequestChannelTests(unittest.TestCase):
    def test_stale_sequence_is_ignored_before_matching_reply(self) -> None:
        request = Message(TYPE_WORKLOAD_CONTROL, 7, bytes(44))
        stale = hid_output_report(Message(TYPE_WORKLOAD_STATUS, 6, bytes(44)).encode())
        matching = hid_output_report(Message(TYPE_WORKLOAD_STATUS, 7, bytes(44)).encode())
        device = FakeHid([b"", stale, matching])

        result = exchange_hid_request(
            device,
            request,
            timeout=0.1,
            expected_processor=None,
            service_transport=lambda: None,
        )

        self.assertIsNone(result.error)
        self.assertIsNotNone(result.reply)
        self.assertEqual(result.reply.sequence, 7)
        self.assertEqual(device.writes, [hid_output_report(request.encode())])


if __name__ == "__main__":
    unittest.main()
