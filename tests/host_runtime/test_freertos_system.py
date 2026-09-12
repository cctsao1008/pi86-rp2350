import struct
import sys
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.freertos_system import (  # noqa: E402
    FreeRTOSSystemTelemetry,
    decode_sequence,
    stable_sequence,
)


class FreeRTOSSystemTelemetryTests(unittest.TestCase):
    def test_decode_and_format(self) -> None:
        raw = struct.pack("<8H", 1, 42, 18, 18, 100, 5, 18, 0)
        snapshot = FreeRTOSSystemTelemetry.decode(raw)
        self.assertEqual(snapshot.led_state, 1)
        self.assertEqual(snapshot.led_toggle_count, 42)
        self.assertEqual(snapshot.queue_tx_count, 18)
        self.assertEqual(snapshot.queue_rx_count, 18)
        self.assertEqual(snapshot.event_name, "QUEUE_RECV")
        self.assertTrue(snapshot.stable)
        rendered = snapshot.format(0x128D8)
        self.assertIn("LED          ON", rendered)
        self.assertIn("QUEUE_RECV", rendered)
        self.assertIn("Errors       0", rendered)

    def test_odd_sequence_is_unstable(self) -> None:
        self.assertFalse(stable_sequence(101))
        self.assertTrue(stable_sequence(102))

    def test_sequence_is_little_endian(self) -> None:
        self.assertEqual(decode_sequence(b"\x34\x12"), 0x1234)

    def test_invalid_sizes_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "exactly 16 bytes"):
            FreeRTOSSystemTelemetry.decode(b"\x00" * 15)
        with self.assertRaisesRegex(ValueError, "exactly 2 bytes"):
            decode_sequence(b"\x00")


if __name__ == "__main__":
    unittest.main()
