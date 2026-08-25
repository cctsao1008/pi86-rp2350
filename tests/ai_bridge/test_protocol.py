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
    COMMAND_REPLY,
    HEARTBEAT_REPLY,
    HeartbeatStats,
    _status_text,
    build_parser,
    heartbeat_payload,
    hid_output_report,
    normalize_hid_input,
    simulate_v30,
    validate_live_reply,
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

    def test_companion_heartbeat_reply_uses_the_same_abi(self) -> None:
        record = Message(TYPE_HEARTBEAT, 9, b"V30 HEARTBEAT OK").encode()
        reply = validate_reply(
            record, 9, TYPE_HEARTBEAT, b"V30 HEARTBEAT OK"
        )
        self.assertEqual(reply.payload, b"V30 HEARTBEAT OK")

    def test_live_heartbeat_has_exactly_seven_fresh_v30_words(self) -> None:
        payload = heartbeat_payload(0x12345678, 0x0102030405060708)
        self.assertEqual(len(payload), 14)
        self.assertEqual(payload[:2], b"HB")
        self.assertEqual(payload[2:6], b"\x78\x56\x34\x12")
        self.assertEqual(payload[6:], b"\x08\x07\x06\x05\x04\x03\x02\x01")

    def test_live_reply_is_bound_to_request_type_and_sequence(self) -> None:
        heartbeat = Message(TYPE_HEARTBEAT, 17, heartbeat_payload(17, 1))
        heartbeat_reply = Message(TYPE_HEARTBEAT, 17, HEARTBEAT_REPLY).encode()
        self.assertEqual(
            validate_live_reply(heartbeat_reply, heartbeat).payload,
            HEARTBEAT_REPLY,
        )
        command = Message(TYPE_COMMAND, 18, b"STATUS")
        command_reply = Message(TYPE_RESULT, 18, COMMAND_REPLY).encode()
        self.assertEqual(
            validate_live_reply(command_reply, command).payload,
            COMMAND_REPLY,
        )
        with self.assertRaises(ValueError):
            validate_live_reply(heartbeat_reply, command)

    def test_heartbeat_statistics_do_not_hide_losses(self) -> None:
        stats = HeartbeatStats()
        stats.accept(12.0)
        stats.accept(18.0)
        stats.lost += 1
        self.assertEqual(stats.completed, 2)
        self.assertEqual(stats.lost, 1)
        self.assertEqual(stats.minimum_ms, 12.0)
        self.assertEqual(stats.average_ms, 15.0)
        self.assertEqual(stats.maximum_ms, 18.0)

    def test_status_row_is_separate_from_the_command_prompt(self) -> None:
        stats = HeartbeatStats()
        stats.accept(3.3)
        text = _status_text(919, stats, True)
        self.assertEqual(text, "| ● NEC V30 ALIVE  seq=919  last=3.3 ms  lost=0")
        self.assertNotIn("V30>", text)

    def test_status_row_can_name_an_intel_8086(self) -> None:
        stats = HeartbeatStats()
        stats.accept(2.7)
        text = _status_text(55, stats, True, "intel-8086")
        self.assertEqual(text, "| ● INTEL 8086 ALIVE  seq=055  last=2.7 ms  lost=0")

    def test_interactive_monitor_can_attach_without_reset(self) -> None:
        args = build_parser().parse_args(
            [
                "--interactive",
                "--heartbeat",
                "--attach",
                "--port",
                "COM27",
            ]
        )
        self.assertTrue(args.interactive)
        self.assertTrue(args.heartbeat)
        self.assertTrue(args.attach)

    def test_processor_identity_is_explicit_host_metadata(self) -> None:
        args = build_parser().parse_args(
            [
                "--interactive",
                "--heartbeat",
                "--attach",
                "--processor",
                "intel-8086",
                "--port",
                "COM27",
            ]
        )
        self.assertEqual(args.processor, "intel-8086")

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
