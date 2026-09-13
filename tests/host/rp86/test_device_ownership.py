from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.device_ownership import (  # noqa: E402
    DeviceOwnership,
    DeviceOwnershipError,
)


class DeviceOwnershipTests(unittest.TestCase):
    def test_only_one_owner_can_hold_a_device(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"RP86_BROKER_DIR": directory}
        ):
            first = DeviceOwnership("SERIAL-ONE")
            second = DeviceOwnership("SERIAL-ONE")
            first.acquire()
            try:
                with self.assertRaisesRegex(DeviceOwnershipError, "active owner"):
                    second.acquire()
            finally:
                first.release()

    def test_ownership_can_be_reacquired_after_release(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"RP86_BROKER_DIR": directory}
        ):
            first = DeviceOwnership("SERIAL-TWO")
            first.acquire()
            first.release()
            with DeviceOwnership("SERIAL-TWO"):
                pass

    def test_ownership_is_exclusive_across_processes(self) -> None:
        child_code = (
            "from rp86_runtime.device_ownership import DeviceOwnership, "
            "DeviceOwnershipError; "
            "lock=DeviceOwnership('SERIAL-THREE'); "
            "\ntry: lock.acquire()"
            "\nexcept DeviceOwnershipError: raise SystemExit(23)"
            "\nelse: lock.release(); raise SystemExit(0)"
        )
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"RP86_BROKER_DIR": directory}
        ):
            with DeviceOwnership("SERIAL-THREE"):
                environment = os.environ.copy()
                environment["PYTHONPATH"] = str(TOOLS)
                completed = subprocess.run(
                    [sys.executable, "-c", child_code],
                    env=environment,
                    check=False,
                    timeout=5,
                )
        self.assertEqual(completed.returncode, 23)


if __name__ == "__main__":
    unittest.main()
