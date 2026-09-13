from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class TickInterruptPipelineTests(unittest.TestCase):
    def test_executor_and_real_clock_stepped_controller_are_composed(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "test_tick_interrupt_pipeline"
            sources = [
                "tests/firmware/runtime/test_tick_interrupt_pipeline.c",
                "firmware/runtime/workload_executor.c",
                "firmware/runtime/clock_stepped_bus_controller.c",
                "firmware/runtime/workload_manager.c",
                "firmware/memory/backing.c",
                "firmware/memory/memory.c",
                "firmware/memory/shared_mailbox.c",
            ]
            includes = [
                "tests/firmware/runtime/stubs",
                "firmware",
                "third_party/fatfs/source",
            ]
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *(flag for path in includes for flag in ("-I", str(ROOT / path))),
                    *(str(ROOT / path) for path in sources),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)

            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
