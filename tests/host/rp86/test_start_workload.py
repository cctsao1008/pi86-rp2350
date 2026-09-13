from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from host.rp86.start_workload import (  # noqa: E402
    _format_duration,
    _next_sequence,
    _sequence_from_hello,
    _spawn_background_owner,
    _timestamp,
    _workload_path_error,
    build_parser,
)


class StartWorkloadTests(unittest.TestCase):
    def test_parser_exposes_explicit_start_workload_mode(self) -> None:
        args = build_parser().parse_args(
            ["--start-workload", "FREERTOS-SYSTEM.P86W"]
        )
        self.assertEqual(args.start_workload, "FREERTOS-SYSTEM.P86W")
        self.assertEqual(args.timeout, 5.0)

    def test_sequence_uses_broker_snapshot_and_wraps(self) -> None:
        self.assertEqual(
            _sequence_from_hello({"snapshot": {"request_sequence": 37}}),
            37,
        )
        self.assertEqual(_next_sequence(37), 38)
        self.assertEqual(_next_sequence(0xFFFFFFFF), 1)

    def test_start_workload_requires_existing_p86w_file(self) -> None:
        self.assertIsNotNone(_workload_path_error("missing.P86W"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            p86w = root / "RTOS.P86W"
            p86w.write_bytes(b"placeholder")
            self.assertIsNone(_workload_path_error(str(p86w)))
            binary = root / "RTOS.BIN"
            binary.write_bytes(b"placeholder")
            self.assertIn(".P86W", _workload_path_error(str(binary)) or "")

    def test_background_owner_uses_canonical_module_entry_point(self) -> None:
        process = object()
        with patch("host.rp86.start_workload.subprocess.Popen", return_value=process) as popen:
            self.assertIs(_spawn_background_owner(), process)
        command = popen.call_args.args[0]
        kwargs = popen.call_args.kwargs
        self.assertEqual(
            command[:3],
            [sys.executable, "-m", "host.apps.cli.rp86"],
        )
        self.assertEqual(Path(kwargs["cwd"]), ROOT)
        self.assertIs(kwargs["stdin"], subprocess.DEVNULL)
        self.assertIs(kwargs["stdout"], subprocess.DEVNULL)
        self.assertIs(kwargs["stderr"], subprocess.DEVNULL)

    def test_lifecycle_timestamp_is_local_offset_aware_to_milliseconds(self) -> None:
        self.assertRegex(
            _timestamp(),
            re.compile(
                r"^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} "
                r"[+-]\d{2}:\d{2}\]$"
            ),
        )

    def test_deployment_duration_uses_millisecond_resolution(self) -> None:
        self.assertEqual(_format_duration(19.3154), "19.315 s")
        self.assertEqual(_format_duration(-1.0), "0.000 s")


if __name__ == "__main__":
    unittest.main()
