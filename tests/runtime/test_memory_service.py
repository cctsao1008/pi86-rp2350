from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class MemoryServiceTests(unittest.TestCase):
    def test_shared_mailbox_and_memory_service(self) -> None:
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "test_memory_service"
            sources = (
                ROOT / "tests/runtime/test_memory_service.c",
                ROOT / "firmware/memory/backing.c",
                ROOT / "firmware/memory/memory_service.c",
                ROOT / "firmware/memory/shared_mailbox.c",
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
