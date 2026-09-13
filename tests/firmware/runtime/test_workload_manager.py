from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WorkloadManagerTests(unittest.TestCase):
    def test_addressed_internal_backing_and_upload_integrity(self) -> None:
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "test_workload_manager"
            sources = [
                ROOT / "tests" / "runtime" / "test_workload_manager.c",
                ROOT / "firmware" / "memory" / "backing.c",
                ROOT / "firmware" / "runtime" / "workload_manager.c",
            ]
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware"),
                    *(str(source) for source in sources),
                    "-o",
                    str(executable),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                0,
                compile_result.returncode,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)], check=False, capture_output=True, text=True
            )
            self.assertEqual(
                0, run_result.returncode, run_result.stdout + run_result.stderr
            )


if __name__ == "__main__":
    unittest.main()
