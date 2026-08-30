from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.cli import build_parser  # noqa: E402
from rp86_runtime.console import ConsoleStatus, _apply_input_character  # noqa: E402
from rp86_runtime.device import DeviceClient  # noqa: E402
from rp86_runtime.broker import broker_registry_dirs  # noqa: E402
from rp86_runtime.calculator import calculator_payload, is_calculator_payload  # noqa: E402
from rp86_runtime.session import (  # noqa: E402
    _format_runtime_top,
    _prepared_heartbeat_is_available,
    _processor_execution_state,
)
from rp86_runtime.shell_commands import (  # noqa: E402
    complete_shell_input,
    host_list_path,
    parse_command,
)
from rp86_runtime.workload import WorkloadManifest  # noqa: E402
import rp86_web  # noqa: E402


class Rp86RuntimeTests(unittest.TestCase):
    def test_canonical_entrypoint_does_not_monkey_patch_runtime_modules(self) -> None:
        source = (TOOLS / "rp86.py").read_text(encoding="utf-8")
        self.assertNotIn("decode_status_payload =", source)
        self.assertNotIn("DeviceBroker.publish =", source)
        self.assertNotIn("rp86_web._", source)

    def test_web_owner_starts_canonical_exchange_and_heartbeat(self) -> None:
        process = Mock()
        process.poll.return_value = None
        record = SimpleNamespace(device_id="TEST-RP86")
        rp86_web._owned_runtime = None
        rp86_web._owner_mode = "not-started"
        rp86_web._owner_error = None
        with (
            patch.object(rp86_web, "_active_broker", side_effect=[None, record]),
            patch.object(rp86_web.subprocess, "Popen", return_value=process) as popen,
            patch.object(rp86_web.time, "sleep"),
        ):
            result = rp86_web._ensure_runtime_owner(wait_seconds=0.5)

        self.assertTrue(result["ok"])
        self.assertEqual(result["mode"], "web-owned")
        command = popen.call_args.args[0]
        self.assertIn("--exchange", command)
        self.assertIn("--heartbeat", command)
        self.assertNotIn("--attach", command)
        rp86_web._owned_runtime = None
        rp86_web._owner_mode = "not-started"
        rp86_web._owner_error = None

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
            connected=True,
            completed=25,
            lost=0,
            average_ms=2.6,
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
        self.assertEqual(_processor_execution_state(2, 1), "IDLE / HLT")
        self.assertEqual(_processor_execution_state(3, 0), "STOPPED / RESET")
        self.assertEqual(_processor_execution_state(2, 0), "ACTIVE")

    def test_heartbeat_requires_the_prepared_free_running_responder(self) -> None:
        self.assertTrue(_prepared_heartbeat_is_available(0))
        self.assertTrue(_prepared_heartbeat_is_available(1))
        self.assertFalse(_prepared_heartbeat_is_available(2))
        self.assertFalse(_prepared_heartbeat_is_available(3))


if __name__ == "__main__":
    unittest.main()
