import sys
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.shell_commands import (  # noqa: E402
    CommandHistory,
    command_help,
    complete_shell_input,
    format_host_directory,
    host_list_path,
    is_device_path,
    parse_command,
    unavailable_message,
)


class HostShellTests(unittest.TestCase):
    def test_complete_framework_commands_are_registered(self) -> None:
        for name in (
            "load", "run", "stop", "restart", "bootloader", "send", "console", "ls",
            "put", "get", "df", "mount", "unmount", "sync", "mem", "top",
            "trace", "selftest", "timeout", "status", "quit",
            "mailbox",
        ):
            self.assertEqual(parse_command(name).spec.name, name)

    def test_flash_and_sd_paths_remain_arguments(self) -> None:
        flash = parse_command("ls flash:/workloads")
        sd = parse_command('put "C:/My Files/demo.bin" sd:/demo.bin')
        self.assertEqual(flash.arguments, ("flash:/workloads",))
        self.assertEqual(sd.arguments, ("C:/My Files/demo.bin", "sd:/demo.bin"))

    def test_windows_backslashes_are_not_treated_as_escapes(self) -> None:
        command = parse_command(r'ls "C:\Program Files"')
        self.assertEqual(command.arguments, (r"C:\Program Files",))

    def test_device_and_host_paths_are_distinct(self) -> None:
        self.assertTrue(is_device_path("flash:/workloads"))
        self.assertTrue(is_device_path("sd:/"))
        self.assertFalse(is_device_path("C:/workloads"))
        self.assertFalse(is_device_path("/home/build"))

    def test_host_directory_listing_is_cross_platform(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "folder").mkdir()
            (root / "hello.txt").write_text("hello", encoding="utf-8")
            rendered = format_host_directory(str(root))
        self.assertIn("Directory of Host", rendered)
        self.assertIn("<DIR>  folder", rendered)
        self.assertIn("5  hello.txt", rendered)

    def test_command_and_local_path_completion(self) -> None:
        completed, candidates = complete_shell_input("sta")
        self.assertEqual(completed, "status ")
        self.assertEqual(candidates, ("status",))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "hello.txt").write_text("hello", encoding="utf-8")
            partial = f"ls {root / 'hel'}"
            completed, candidates = complete_shell_input(partial)
        self.assertTrue(completed.endswith("hello.txt"))
        self.assertEqual(len(candidates), 1)

    def test_device_completion_uses_supplied_directory_entries(self) -> None:
        completed, candidates = complete_shell_input(
            "cat flash:/HOST", (("HOSTTEST.TXT", False), ("OTHER", False))
        )
        self.assertEqual(completed, "cat flash:/HOSTTEST.TXT")
        self.assertEqual(candidates, ("flash:/HOSTTEST.TXT",))

    def test_bare_windows_drive_is_rooted(self) -> None:
        rooted = str(host_list_path("C:"))
        self.assertTrue(rooted.startswith("C:"))

    def test_history_moves_up_down_and_restores_draft(self) -> None:
        history = CommandHistory()
        history.remember("status")
        history.remember("ls flash:/")
        self.assertEqual(history.move("sta", -1), "ls flash:/")
        self.assertEqual(history.move("ls flash:/", -1), "status")
        self.assertEqual(history.move("status", 1), "ls flash:/")
        self.assertEqual(history.move("ls flash:/", 1), "sta")

    def test_history_suppresses_adjacent_duplicates(self) -> None:
        history = CommandHistory()
        history.remember("status")
        history.remember("status")
        self.assertEqual(history.move("", -1), "status")
        self.assertEqual(history.move("status", -1), "status")

    def test_aliases_are_canonicalized(self) -> None:
        self.assertEqual(parse_command("exit").spec.name, "quit")
        self.assertEqual(parse_command("?").spec.name, "help")
        self.assertEqual(parse_command("bootsel").spec.name, "bootloader")

    def test_unknown_command_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "unknown command"):
            parse_command("format-c-drive")

    def test_unavailable_backend_is_explicit(self) -> None:
        message = unavailable_message(parse_command("mount sd:"))
        self.assertIn("NOT AVAILABLE", message)
        self.assertIn("'sd'", message)

    def test_help_exposes_both_storage_backends(self) -> None:
        help_text = command_help()
        self.assertIn("flash:", help_text)
        self.assertIn("sd:", help_text)
        self.assertIn("mem read|write|load|save", help_text)


if __name__ == "__main__":
    unittest.main()
