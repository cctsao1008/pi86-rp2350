from pathlib import Path
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.cli import build_parser  # noqa: E402
from rp86_runtime.console import ConsoleStatus, _apply_input_character  # noqa: E402
from rp86_runtime.device import DeviceClient  # noqa: E402
from rp86_runtime.broker import broker_registry_dirs  # noqa: E402
from rp86_runtime.session import _format_runtime_top  # noqa: E402
from rp86_runtime.shell_commands import (  # noqa: E402
    complete_shell_input,
    host_list_path,
    parse_command,
)
from rp86_runtime.workload import WorkloadManifest  # noqa: E402


class Rp86RuntimeTests(unittest.TestCase):
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

    def test_rpbridge_discovers_legacy_registry_during_transition(self) -> None:
        names = {path.name for path in broker_registry_dirs()}
        self.assertIn("rp86-brokers", names)
        self.assertIn("pi86-rp2350-brokers", names)

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
            manifest=manifest,
        )
        self.assertIn("Workload   READY id=1 size=157 bytes", output)
        self.assertIn("Load       0x10000 entry=1000:0000", output)
        self.assertIn("Memory     INTERNAL SRAM 00000-3FFFF 256 KiB", output)
        self.assertIn("PSRAM      NOT AVAILABLE (optional expansion)", output)
        self.assertNotIn("state=2", output)
        self.assertNotIn("detail=157", output)


if __name__ == "__main__":
    unittest.main()
