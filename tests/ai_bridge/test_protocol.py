from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[2] / "tools" / "ai_bridge"
sys.path.insert(0, str(TOOLS))

from protocol import (  # noqa: E402
    FLAG_RETRY,
    MESSAGE_SIZE,
    NATIVE_PROCESSOR_INTEL_8086,
    NATIVE_PROCESSOR_NEC_V30,
    STATUS_TIMEOUT,
    Message,
    NativeServiceWitness,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    TYPE_HELLO,
    TYPE_RESULT,
    TYPE_TEXT,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_RESULT,
)
from v30bridge import (  # noqa: E402
    BOOTLOADER_ACK,
    BOOTLOADER_REQUEST,
    STATUS_BEGIN,
    STATUS_END,
    STATUS_REQUEST,
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
    cdc_serial_for_port,
    resolve_cdc_port,
    select_cdc_port,
    send_bootloader_request,
    send_status_request,
    simulate_v30,
    validate_live_reply,
    validate_device_reply,
    validate_reply,
)


class ProtocolTests(unittest.TestCase):
    def test_cdc_bootloader_request_requires_exact_ack(self) -> None:
        class FakeConnection:
            def __init__(self) -> None:
                self.written = b""
                self.response = bytearray(BOOTLOADER_ACK)

            def write(self, data: bytes) -> int:
                self.written += data
                return len(data)

            def flush(self) -> None:
                pass

            @property
            def in_waiting(self) -> int:
                return len(self.response)

            def read(self, length: int) -> bytes:
                result = bytes(self.response[:length])
                del self.response[:length]
                return result

        connection = FakeConnection()
        evidence = send_bootloader_request(connection, 0.1)
        self.assertEqual(connection.written, BOOTLOADER_REQUEST)
        self.assertIn(BOOTLOADER_ACK, evidence)

    def test_bootloader_has_a_cdc_only_command_line_mode(self) -> None:
        args = build_parser().parse_args(["--bootloader", "--port", "COM14"])
        self.assertTrue(args.bootloader)
        self.assertEqual(args.port, "COM14")

    def test_cdc_status_request_requires_a_complete_framed_block(self) -> None:
        class FakeConnection:
            def __init__(self) -> None:
                self.written = b""
                self.response = bytearray(
                    b"old boot text\r\n"
                    + STATUS_BEGIN
                    + b"\r\n[RUNTIME STATUS]\r\nState = READY\r\n"
                    + STATUS_END
                    + b"\r\n"
                )

            def write(self, data: bytes) -> int:
                self.written += data
                return len(data)

            def flush(self) -> None:
                pass

            def read(self, length: int) -> bytes:
                result = bytes(self.response[:length])
                del self.response[:length]
                return result

        connection = FakeConnection()
        evidence = send_status_request(connection, 0.1)
        self.assertEqual(connection.written, STATUS_REQUEST)
        self.assertTrue(evidence.startswith(STATUS_BEGIN))
        self.assertTrue(evidence.endswith(STATUS_END))
        self.assertNotIn(b"old boot text", evidence)

    def test_status_has_a_cdc_only_command_line_mode(self) -> None:
        args = build_parser().parse_args(["--status", "--port", "COM14"])
        self.assertTrue(args.status)
        self.assertEqual(args.port, "COM14")

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
        payload = NativeServiceWitness(
            TYPE_HEARTBEAT, 7, 0x12345678, 3, HEARTBEAT_REPLY
        ).encode()
        request = Message(TYPE_HEARTBEAT, 9, heartbeat_payload(9, 1))
        reply = validate_live_reply(
            Message(TYPE_HEARTBEAT, 9, payload).encode(), request
        )
        witness = NativeServiceWitness.decode(reply.payload)
        self.assertEqual(witness.boot_id, 7)
        self.assertEqual(witness.cpu_sequence, 0x12345678)
        self.assertEqual(witness.command_sequence, 3)
        self.assertEqual(witness.text, HEARTBEAT_REPLY)

    def test_native_witness_carries_physical_aad_processor_identity(self) -> None:
        intel = NativeServiceWitness(
            TYPE_HEARTBEAT,
            1,
            2,
            0,
            HEARTBEAT_REPLY,
            flags=NATIVE_PROCESSOR_INTEL_8086,
        )
        nec = NativeServiceWitness(
            TYPE_HEARTBEAT,
            1,
            2,
            0,
            HEARTBEAT_REPLY,
            flags=NATIVE_PROCESSOR_NEC_V30,
        )
        self.assertEqual(
            NativeServiceWitness.decode(intel.encode()).processor,
            "intel-8086",
        )
        self.assertEqual(
            NativeServiceWitness.decode(nec.encode()).processor,
            "nec-v30",
        )

    def test_host_processor_declaration_must_match_native_identity(self) -> None:
        request = Message(TYPE_HEARTBEAT, 9, heartbeat_payload(9, 1))
        payload = NativeServiceWitness(
            TYPE_HEARTBEAT,
            1,
            2,
            0,
            HEARTBEAT_REPLY,
            flags=NATIVE_PROCESSOR_INTEL_8086,
        ).encode()
        record = Message(TYPE_HEARTBEAT, 9, payload).encode()
        validate_live_reply(record, request, "intel-8086")
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            validate_live_reply(record, request, "nec-v30")

    def test_companion_initial_hello_reply_uses_native_witness(self) -> None:
        request = Message(TYPE_HELLO, 1, CANONICAL_GREETING)
        payload = NativeServiceWitness(
            TYPE_HELLO, 3, 8, 1, HEARTBEAT_REPLY
        ).encode()
        reply = validate_live_reply(
            Message(TYPE_HEARTBEAT, 1, payload).encode(), request
        )
        witness = NativeServiceWitness.decode(reply.payload)
        self.assertEqual(witness.boot_id, 3)
        self.assertEqual(witness.cpu_sequence, 8)
        self.assertEqual(witness.text, HEARTBEAT_REPLY)

    def test_native_witness_rejects_wrong_service_type(self) -> None:
        request = Message(TYPE_HEARTBEAT, 9, heartbeat_payload(9, 1))
        payload = NativeServiceWitness(
            TYPE_COMMAND, 1, 2, 3, HEARTBEAT_REPLY
        ).encode()
        with self.assertRaisesRegex(ValueError, "service type mismatch"):
            validate_live_reply(
                Message(TYPE_HEARTBEAT, 9, payload).encode(), request
            )

    def test_live_heartbeat_has_exactly_seven_fresh_v30_words(self) -> None:
        payload = heartbeat_payload(0x12345678, 0x0102030405060708)
        self.assertEqual(len(payload), 14)
        self.assertEqual(payload[:2], b"HB")
        self.assertEqual(payload[2:6], b"\x78\x56\x34\x12")
        self.assertEqual(payload[6:], b"\x08\x07\x06\x05\x04\x03\x02\x01")

    def test_live_reply_is_bound_to_request_type_and_sequence(self) -> None:
        heartbeat = Message(TYPE_HEARTBEAT, 17, heartbeat_payload(17, 1))
        heartbeat_reply = Message(
            TYPE_HEARTBEAT,
            17,
            NativeServiceWitness(
                TYPE_HEARTBEAT, 1, 9, 0, HEARTBEAT_REPLY
            ).encode(),
        ).encode()
        self.assertEqual(
            NativeServiceWitness.decode(
                validate_live_reply(heartbeat_reply, heartbeat).payload
            ).text,
            HEARTBEAT_REPLY,
        )
        command = Message(TYPE_COMMAND, 18, b"STATUS")
        command_reply = Message(
            TYPE_RESULT,
            18,
            NativeServiceWitness(
                TYPE_COMMAND, 1, 10, 1, COMMAND_REPLY
            ).encode(),
        ).encode()
        self.assertEqual(
            NativeServiceWitness.decode(
                validate_live_reply(command_reply, command).payload
            ).text,
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
        self.assertEqual(
            text,
            "| ● NEC V30 ALIVE  cpu_seq=000919  rtt=3.3 ms  lost=0",
        )
        self.assertNotIn("V30>", text)

    def test_status_row_can_name_an_intel_8086(self) -> None:
        stats = HeartbeatStats()
        stats.accept(2.7)
        text = _status_text(55, stats, True, "intel-8086")
        self.assertEqual(
            text,
            "| ● INTEL 8086 ALIVE  cpu_seq=000055  rtt=2.7 ms  lost=0",
        )

    def test_cdc_port_selection_defaults_to_the_only_composite_device(self) -> None:
        candidates = [
            {
                "port": "COM27",
                "serial": "A1D538EA0A07378F",
                "description": "USB Serial Device",
            }
        ]
        self.assertEqual(select_cdc_port(candidates), "COM27")
        self.assertEqual(
            select_cdc_port(candidates, "A1D538EA0A07378F"), "COM27"
        )

    def test_cdc_port_selection_refuses_ambiguous_devices(self) -> None:
        candidates = [
            {"port": "COM14", "serial": "ONE", "description": "first"},
            {"port": "COM27", "serial": "TWO", "description": "second"},
        ]
        with self.assertRaisesRegex(RuntimeError, "Use --port COMxx"):
            select_cdc_port(candidates)
        self.assertEqual(select_cdc_port(candidates, "TWO"), "COM27")

    def test_explicit_cdc_port_bypasses_discovery(self) -> None:
        self.assertEqual(resolve_cdc_port("COM14"), ("COM14", False))

    def test_cdc_port_pairs_the_matching_hid_serial(self) -> None:
        candidates = [
            {"port": "COM14", "serial": "ONE", "description": "first"},
            {"port": "COM27", "serial": "TWO", "description": "second"},
        ]
        self.assertEqual(cdc_serial_for_port("com27", candidates), "TWO")
        self.assertIsNone(cdc_serial_for_port("COM31", candidates))

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

    def test_workload_reply_is_sequence_bound_without_text_magic(self) -> None:
        request = Message(TYPE_WORKLOAD_BEGIN, 123, b"manifest")
        reply = Message(TYPE_WORKLOAD_RESULT, 123, b"accepted").encode()
        self.assertEqual(
            validate_device_reply(reply, request).payload, b"accepted"
        )
        with self.assertRaisesRegex(ValueError, "sequence mismatch"):
            validate_device_reply(
                Message(TYPE_WORKLOAD_RESULT, 124, b"accepted").encode(), request
            )


if __name__ == "__main__":
    unittest.main()
