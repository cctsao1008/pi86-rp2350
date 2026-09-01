from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class CdcCommandParserTests(unittest.TestCase):
    def test_text_commands_and_overflow(self) -> None:
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "test_cdc_command_parser"
            sources = (
                ROOT / "tests/runtime/test_cdc_command_parser.c",
                ROOT / "firmware/runtime/cdc_command_parser.c",
            )
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "firmware"),
                 *(str(source) for source in sources), "-o", str(executable)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
