from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[2] / "tools" / "ai_bridge"
sys.path.insert(0, str(TOOLS))

from protocol import (  # noqa: E402
    FLAG_RETRY,
    MESSAGE_SIZE,
    STATUS_TIMEOUT,
    Message,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    TYPE_HELLO,
    TYPE_RESULT,
    TYPE_TEXT,
)
from v30bridge import (  # noqa: E402
    CANONICAL_GREETING,
    CANONICAL_REPLY,
    hid_output_report,
    normalize_hid_input,
    simulate_v30,
    validate_reply,
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

    def test_hid_output_has_report_id_and_exact_abi(self) -> None:
        record = Message(TYPE_HELLO, 1, CANONICAL_GREETING).encode()
        report = hid_output_report(record)
        self.assertEqual(len(report), MESSAGE_SIZE + 1)
        self.assertEqual(report[0], 0)
        self.assertEqual(report[1:], record)

    def test_hid_input_accepts_platform_report_id(self) -> None:
        record = Message(TYPE_TEXT, 1, CANONICAL_REPLY).encode()
        self.assertEqual(normalize_hid_input(record), record)
        self.assertEqual(normalize_hid_input(b"\0" + record), record)

    def test_physical_reply_requires_sequence_and_payload(self) -> None:
        valid = Message(TYPE_TEXT, 7, CANONICAL_REPLY).encode()
        self.assertEqual(validate_reply(valid, 7).payload, CANONICAL_REPLY)
        with self.assertRaises(ValueError):
            validate_reply(valid, 8)
        wrong = Message(TYPE_TEXT, 7, b"RECORDED STRING").encode()
        with self.assertRaises(ValueError):
            validate_reply(wrong, 7)

    def test_runtime_messages_preserve_the_version_one_layout(self) -> None:
        records = (
            Message(TYPE_COMMAND, 40, b"STATUS"),
            Message(TYPE_RESULT, 40, b"READY"),
            Message(TYPE_HEARTBEAT, 41, b"uptime=123"),
        )
        for message in records:
            encoded = message.encode()
            self.assertEqual(len(encoded), MESSAGE_SIZE)
            self.assertEqual(Message.decode(encoded), message)

    def test_retry_and_timeout_are_explicit_wire_fields(self) -> None:
        retry = Message(TYPE_COMMAND, 99, b"STATUS", flags=FLAG_RETRY)
        timed_out = Message(
            TYPE_RESULT, 99, b"", flags=FLAG_RETRY, status=STATUS_TIMEOUT
        )
        self.assertEqual(Message.decode(retry.encode()).flags, FLAG_RETRY)
        decoded_timeout = Message.decode(timed_out.encode())
        self.assertEqual(decoded_timeout.sequence, retry.sequence)
        self.assertEqual(decoded_timeout.status, STATUS_TIMEOUT)


if __name__ == "__main__":
    unittest.main()
