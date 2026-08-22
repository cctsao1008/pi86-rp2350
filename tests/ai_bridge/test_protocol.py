from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[2] / "tools" / "ai_bridge"
sys.path.insert(0, str(TOOLS))

from protocol import MESSAGE_SIZE, Message, TYPE_HELLO, TYPE_TEXT  # noqa: E402
from v30bridge import (  # noqa: E402
    CANONICAL_GREETING,
    CANONICAL_REPLY,
    simulate_v30,
)


class ProtocolTests(unittest.TestCase):
    def test_record_is_fixed_size_and_round_trips(self) -> None:
        message = Message(TYPE_HELLO, 0x12345678, CANONICAL_GREETING)
        record = message.encode()
        self.assertEqual(len(record), MESSAGE_SIZE)
        self.assertEqual(Message.decode(record), message)

    def test_canonical_exchange(self) -> None:
        request = Message(TYPE_HELLO, 7, CANONICAL_GREETING)
        reply = Message.decode(simulate_v30(request.encode()))
        self.assertEqual(reply.message_type, TYPE_TEXT)
        self.assertEqual(reply.sequence, request.sequence)
        self.assertEqual(reply.payload, CANONICAL_REPLY)

    def test_oversize_payload_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            Message(TYPE_TEXT, 1, b"x" * 53).encode()

    def test_bad_record_size_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            Message.decode(b"short")


if __name__ == "__main__":
    unittest.main()
