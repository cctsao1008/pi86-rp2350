from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.cli import build_parser, _regression_workload_error  # noqa: E402
from rp86_runtime.console import (  # noqa: E402
    CdcDisplayStream,
    ConsoleStatus,
    _apply_input_character,
    _status_text,
)
from rp86_runtime.device import DeviceClient  # noqa: E402
from rp86_runtime.broker import broker_registry_dirs  # noqa: E402
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
    host_list_path,
    parse_command,
)
from rp86_runtime.workload import WorkloadManifest  # noqa: E402
import rp86_web  # noqa: E402
import rp86_web_api  # noqa: E402


class Rp86RuntimeTests(unittest.TestCase):
    def test_canonical_entrypoint_does_not_monkey_patch_runtime_modules(self) -> None:
        source = (TOOLS / "rp86.py").read_text(encoding="utf-8")
        self.assertNotIn("decode_status_payload =", source)
        self.assertNotIn("DeviceBroker.publish =", source)
        self.assertNotIn("rp86_web._", source)

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
        self.assertNotIn("--exchange", command)

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

    def test_calculator_is_a_first_class_shell_command(self) -> None:
        command = parse_command("calc 123 + 456")
        self.assertEqual(command.spec.name, "calc")
        self.assertEqual(command.arguments, ("123", "+", "456"))

    def test_processor_identity_defaults_to_native_auto_detection(self) -> None:
        args = build_parser().parse_args(["--interactive"])
        self.assertEqual(args.processor, "auto")

    def test_new_cli_keeps_existing_modes(self) -> None:
        args = build_parser().parse_args(["--interactive", "--processor", "intel-8086"])
        self.assertTrue(args.interactive)
        self.assertEqual(args.processor, "intel-8086")

    def test_physical_regression_is_a_single_workload_mode(self) -> None:
        args = build_parser().parse_args(
            ["--physical-regression", "build/workloads/INVSQRT.P86W"]
        )
        self.assertEqual(
            args.physical_regression,
            "build/workloads/INVSQRT.P86W",
        )

    def test_physical_regression_rejects_a_missing_host_file_early(self) -> None:
        self.assertIsNotNone(
            _regression_workload_error("missing/INVSQRT.P86W")
        )
        self.assertIsNone(_regression_workload_error("flash:/INVSQRT.P86W"))

    def test_cli_accepts_live_and_plain_renderers(self) -> None:
        self.assertEqual(
            build_parser().parse_args(["--interactive"]).display,
            "live",
        )
        self.assertEqual(
            build_parser().parse_args(["--interactive", "--display", "live"]).display,
            "live",
        )
        self.assertEqual(
            build_parser().parse_args(["--interactive", "--display", "plain"]).display,
            "plain",
        )

    def test_generic_interactive_runtime_does_not_enable_native_probe(self) -> None:
        args = build_parser().parse_args(["--interactive", "--attach"])
        self.assertFalse(args.native_probe)
        self.assertFalse(args.monitor)

    def test_console_adopts_native_processor_identity(self) -> None:
        console = ConsoleStatus("auto")
        self.assertEqual(console._prompt, "CPU")
        console.set_processor("intel-8086")
        self.assertEqual(console._prompt, "8086")
        console.set_processor("nec-v30")
        self.assertEqual(console._prompt, "V30")

    def test_pwd_and_cd_are_first_class_commands(self) -> None:
        self.assertEqual(parse_command("pwd").spec.name, "pwd")
        command = parse_command('cd "some directory"')
        self.assertEqual(command.spec.name, "cd")
        self.assertEqual(command.arguments, ("some directory",))

    def test_host_paths_resolve_relative_to_shell_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            self.assertEqual(host_list_path("child", base), base / "child")

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

    def test_broker_uses_one_canonical_registry(self) -> None:
        self.assertEqual(
            tuple(path.name for path in broker_registry_dirs()),
            ("rp86-brokers",),
        )

    def test_top_explains_internal_sram_workload_state(self) -> None:
        image = bytes(range(157))
        manifest = WorkloadManifest.for_image(
            image,
            load_address=0x10000,
            entry_segment=0x1000,
            entry_offset=0,
        )
        output = _format_runtime_top(
            processor_name="INTEL 8086",
            processor_identified=True,
            workload_id=1,
            workload_state=2,
            workload_detail=len(image),
            workload_clock_mode=2,
            workload_cycles=123,
            workload_processor_flags=1,
            manifest=manifest,
        )
        self.assertIn("Workload   STAGED id=1 size=157 bytes", output)
        self.assertIn("Load       0x10000 entry=1000:0000", output)
        self.assertIn("Memory     INTERNAL SRAM 00000-3FFFF 256 KiB", output)
        self.assertIn("PSRAM      NOT AVAILABLE (optional expansion)", output)
        self.assertIn("Clock mode CLOCK-STEPPED", output)
        self.assertIn("CPU cycles 123", output)
        self.assertIn("Processor IDLE / HLT", output)
        self.assertNotIn("state=2", output)
        self.assertNotIn("detail=157", output)

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
        self.assertIn("CPU cycles 3212", output)
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
