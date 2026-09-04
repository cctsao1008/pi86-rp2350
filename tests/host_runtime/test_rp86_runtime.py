from pathlib import Path
from contextlib import redirect_stdout
import io
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.cli import build_parser, _regression_workload_error  # noqa: E402
import rp86_runtime.cli as cli  # noqa: E402
from rp86_runtime.console import (  # noqa: E402
    CdcDisplayStream,
    _apply_input_character,
    _status_text,
)
from rp86_runtime.device import DeviceClient  # noqa: E402
from rp86_runtime.calculator import calculator_payload, is_calculator_payload  # noqa: E402
from rp86_runtime.session import (  # noqa: E402
    _broker_runtime_state,
    _native_probe_unavailable,
    _format_runtime_top,
)
from rp86_runtime.runtime_state import (  # noqa: E402
    prepared_runtime_is_available,
    processor_execution_state,
    workload_upload_requires_stop,
)
from rp86_runtime.shell_commands import (  # noqa: E402
    complete_shell_input,
)
import rp86_web_api  # noqa: E402


class Rp86RuntimeTests(unittest.TestCase):
    def test_interactive_routes_directly_to_runtime_without_reset(self) -> None:
        for attach in ([], ["--attach"]):
            with (
                self.subTest(attach=attach),
                patch.object(sys, "argv", ["rp86", "--interactive", "--sequence", "37", *attach]),
                patch.object(cli, "discover_brokers", return_value=[]),
                patch.object(cli, "resolve_cdc_port", return_value=("COM-TEST", False)),
                patch.object(cli, "cdc_serial_for_port", return_value="TEST"),
                patch.object(cli, "send_hid_runtime_control") as reset,
                patch.object(cli, "persistent_monitor", return_value=17) as session,
            ):
                self.assertEqual(cli.main(), 17)
                session.assert_called_once()
                self.assertEqual(session.call_args.kwargs["sequence"], 37)
                self.assertEqual(session.call_args.kwargs["port"], "COM-TEST")
                self.assertFalse(session.call_args.kwargs["native_probe"])
                reset.assert_not_called()

    def test_interactive_reuses_broker_even_without_attach_flag(self) -> None:
        record = SimpleNamespace(device_id="TEST", tcp_port=1234)
        with (
            patch.object(sys, "argv", ["rp86", "--interactive"]),
            patch.object(cli, "discover_brokers", return_value=[record]),
            patch.object(cli, "select_broker", return_value=record),
            patch.object(cli, "resolve_cdc_port") as open_port,
            patch.object(cli, "send_hid_runtime_control") as reset,
            patch.object(cli, "persistent_monitor", return_value=0) as session,
            redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(cli.main(), 0)
            self.assertIs(session.call_args.kwargs["broker_record"], record)
            self.assertEqual(session.call_args.kwargs["port"], "")
            open_port.assert_not_called()
            reset.assert_not_called()

    def test_regression_routes_to_structured_workload_session(self) -> None:
        with (
            patch.object(sys, "argv", ["rp86", "--physical-regression", "flash:/INVSQRT.P86W",
                                      "--native-probe", "--rounds", "1"]),
            patch.object(cli, "discover_brokers", return_value=[]),
            patch.object(cli, "resolve_cdc_port", return_value=("COM-TEST", False)),
            patch.object(cli, "cdc_serial_for_port", return_value="TEST"),
            patch.object(cli, "send_hid_runtime_control") as reset,
            patch.object(cli, "persistent_monitor", return_value=5) as session,
        ):
            self.assertEqual(cli.main(), 5)
            options = session.call_args.kwargs
            self.assertEqual(options["regression_workload"], "flash:/INVSQRT.P86W")
            self.assertFalse(options["interactive"])
            self.assertFalse(options["native_probe"])
            self.assertEqual(options["rounds"], 0)
            reset.assert_not_called()

    def test_web_owner_starts_canonical_runtime_monitor(self) -> None:
        process = Mock()
        process.poll.return_value = None
        record = SimpleNamespace(device_id="TEST-RP86")
        api = rp86_web_api.WebApi(TOOLS)
        with (
            patch.object(api, "active_broker", side_effect=[None, record]),
            patch.object(rp86_web_api.subprocess, "Popen", return_value=process) as popen,
            patch.object(rp86_web_api.time, "sleep"),
        ):
            result = api.ensure_runtime_owner(wait_seconds=0.5)

        self.assertTrue(result["ok"])
        self.assertEqual(result["mode"], "web-owned")
        command = popen.call_args.args[0]
        self.assertIn("--interactive", command)
        self.assertIn("--attach", command)
        self.assertNotIn("--heartbeat", command)

    def test_web_owner_recovers_stopped_runtime_once_via_hid_reboot(self) -> None:
        stopped = Mock()
        stopped.poll.return_value = 5
        stopped.returncode = 5
        accepted = Mock()
        accepted.poll.return_value = None
        record = SimpleNamespace(device_id="TEST-RP86")
        api = rp86_web_api.WebApi(TOOLS)
        with (
            patch.object(
                api,
                "active_broker",
                side_effect=[None, None, record],
            ),
            patch.object(
                rp86_web_api.subprocess,
                "Popen",
                side_effect=[stopped, accepted],
            ) as popen,
            patch.object(
                api,
                "run_rp86",
                return_value={"ok": True},
            ) as run_rp86,
            patch.object(rp86_web_api.time, "sleep"),
        ):
            result = api.ensure_runtime_owner(wait_seconds=0.5)

        self.assertTrue(result["ok"])
        self.assertEqual(result["mode"], "web-owned")
        self.assertEqual(popen.call_count, 2)
        run_rp86.assert_called_once_with(
            "--reboot", "--timeout", "5", timeout=8.0
        )

    def test_calculator_encodes_syntax_without_host_evaluation(self) -> None:
        payload = calculator_payload(("0x1234", "*", "3"))
        self.assertTrue(is_calculator_payload(payload))
        self.assertEqual(
            payload,
            bytes.fromhex("1cca030034120300000000000000"),
        )

    def test_compact_send_expression_uses_native_calculator_abi(self) -> None:
        self.assertEqual(
            calculator_payload(("12+34",)), calculator_payload(("12", "+", "34"))
        )

    def test_calculator_rejects_unsafe_native_division(self) -> None:
        with self.assertRaisesRegex(ValueError, "division by zero"):
            calculator_payload(("7/0",))

    def test_processor_identity_defaults_to_native_auto_detection(self) -> None:
        args = build_parser().parse_args(["--interactive"])
        self.assertEqual(args.processor, "auto")

    def test_physical_regression_rejects_a_missing_host_file_early(self) -> None:
        self.assertIsNotNone(
            _regression_workload_error("missing/INVSQRT.P86W")
        )
        self.assertIsNone(_regression_workload_error("flash:/INVSQRT.P86W"))

    def test_generic_interactive_runtime_does_not_enable_native_probe(self) -> None:
        args = build_parser().parse_args(["--interactive", "--attach"])
        self.assertFalse(args.native_probe)
        self.assertFalse(args.monitor)

    def test_completion_uses_shell_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / "alpha.txt").write_text("hello", encoding="utf-8")
            completed, matches = complete_shell_input("cat al", host_base=base)
            self.assertEqual(matches, ("alpha.txt",))
            self.assertEqual(completed, "cat alpha.txt")

    def test_cursor_aware_insert_backspace_and_cancel(self) -> None:
        buffer, cursor, *_ = _apply_input_character("ac", 1, "b")
        self.assertEqual((buffer, cursor), ("abc", 2))
        buffer, cursor, *_ = _apply_input_character(buffer, cursor, "\b")
        self.assertEqual((buffer, cursor), ("ac", 1))
        buffer, cursor, command, changed, clear = _apply_input_character(
            buffer, cursor, "\x03"
        )
        self.assertEqual((buffer, cursor, command, changed, clear), ("", 0, None, True, False))

    def test_device_client_rejects_unknown_control(self) -> None:
        client = object.__new__(DeviceClient)
        with self.assertRaisesRegex(ValueError, "unsupported RP86"):
            client.control("erase-everything")

    def test_device_client_reads_canonical_broker_reply_hex(self) -> None:
        client = object.__new__(DeviceClient)
        client._client = Mock()
        payload = bytes(range(64))
        client._client.exchange.return_value = {"ok": True, "reply_hex": payload.hex()}
        self.assertEqual(client.exchange(payload), payload)
        client._client.exchange.return_value = {"ok": True, "reply_hex": "00"}
        with self.assertRaisesRegex(RuntimeError, "invalid RP86 record length"):
            client.exchange(payload)

    def test_processor_execution_state_distinguishes_hlt_and_reset(self) -> None:
        self.assertEqual(processor_execution_state(2, 1), "IDLE / HLT")
        self.assertEqual(processor_execution_state(3, 0), "STOPPED / RESET")
        self.assertEqual(processor_execution_state(2, 0), "ACTIVE")

    def test_upload_stops_a_running_or_completed_workload(self) -> None:
        self.assertFalse(workload_upload_requires_stop(0))
        self.assertFalse(workload_upload_requires_stop(2))
        self.assertTrue(workload_upload_requires_stop(3))
        self.assertFalse(workload_upload_requires_stop(4))
        self.assertTrue(workload_upload_requires_stop(5))
        self.assertFalse(workload_upload_requires_stop(6))

    def test_broker_runtime_state_uses_transport_not_processor_liveness(self) -> None:
        self.assertEqual(_broker_runtime_state(None), "OWNER_ACTIVE")
        self.assertEqual(_broker_runtime_state("USB disconnected"), "FAULT")

    def test_top_reports_lifecycle_without_workload_heartbeat(self) -> None:
        output = _format_runtime_top(
            processor_name="INTEL 8086",
            processor_identified=True,
            workload_id=1,
            workload_state=5,
            workload_detail=623,
            workload_clock_mode=2,
            workload_cycles=3212,
            workload_processor_flags=1,
            manifest=None,
        )
        self.assertIn("Processor  INTEL 8086 · IDLE / HLT", output)
        self.assertIn("Workload   COMPLETED", output)
        self.assertIn("Identity   NATIVE AAD 16 IDENTIFIED", output)
        self.assertIn("Bus cycles 3212", output)
        self.assertNotIn("Heartbeat", output)
        self.assertNotIn("ALIVE", output)

    def test_native_probe_requires_the_prepared_free_running_responder(self) -> None:
        self.assertTrue(prepared_runtime_is_available(0))
        self.assertTrue(prepared_runtime_is_available(1))
        self.assertFalse(prepared_runtime_is_available(2))
        self.assertFalse(prepared_runtime_is_available(3))
        self.assertFalse(prepared_runtime_is_available(1, workload_state=2))
        self.assertFalse(prepared_runtime_is_available(1, workload_state=3))
        self.assertFalse(
            prepared_runtime_is_available(
                1, workload_state=0, processor_flags=0
            )
        )

    def test_explicit_native_timeout_suspends_instead_of_counting_loss(self) -> None:
        self.assertTrue(
            _native_probe_unavailable(
                "V30 live reply status is not OK: 4"
            )
        )
        self.assertFalse(_native_probe_unavailable("probe timeout"))
        self.assertFalse(_native_probe_unavailable(None))

    def test_workload_status_is_independent_of_native_probe(self) -> None:
        text = _status_text(
            None,
            SimpleNamespace(completed=0, last_ms=0.0, lost=12),
            False,
            "intel-8086",
            workload_state="COMPLETED",
            clock_mode="CLOCK-STEPPED",
            workload_cycles=3212,
            processor_state="IDLE / HLT",
        )
        self.assertIn("workload=COMPLETED", text)
        self.assertIn("cycles=3212", text)
        self.assertIn("processor=IDLE / HLT", text)
        self.assertNotIn("LOST", text)

    def test_cdc_native_output_is_rendered_without_protocol_noise(self) -> None:
        stream = CdcDisplayStream()
        self.assertEqual(stream.feed(b"protocol noise\r\n"), ())
        self.assertEqual(
            stream.feed(
                b"[NATIVE STDOUT] RESULT: PASS\r\n"
                b"[WORKLOAD COMPLETED] armed native HLT indication accepted\r\n"
            ),
            (
                "[NATIVE OUTPUT]",
                "RESULT: PASS",
                "[WORKLOAD COMPLETED] processor=IDLE / HLT",
            ),
        )


if __name__ == "__main__":
    unittest.main()
