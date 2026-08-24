import sys
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ai_bridge"))

from host_shell import command_help, parse_command, unavailable_message  # noqa: E402


class HostShellTests(unittest.TestCase):
    def test_complete_framework_commands_are_registered(self) -> None:
        for name in (
            "load", "run", "stop", "restart", "send", "console", "ls",
            "put", "get", "df", "mount", "unmount", "sync", "mem", "top",
            "trace", "timeout", "status", "quit",
        ):
            self.assertEqual(parse_command(name).spec.name, name)

    def test_flash_and_sd_paths_remain_arguments(self) -> None:
        flash = parse_command("ls flash:/workloads")
        sd = parse_command('put "C:/My Files/demo.bin" sd:/demo.bin')
        self.assertEqual(flash.arguments, ("flash:/workloads",))
        self.assertEqual(sd.arguments, ("C:/My Files/demo.bin", "sd:/demo.bin"))

    def test_aliases_are_canonicalized(self) -> None:
        self.assertEqual(parse_command("exit").spec.name, "quit")
        self.assertEqual(parse_command("?").spec.name, "help")

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
