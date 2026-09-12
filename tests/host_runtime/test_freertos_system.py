import struct
import sys
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.freertos_system import (  # noqa: E402
    FreeRTOSSystemTelemetry,
    counter_delta,
    decode_sequence,
    read_stable_telemetry,
    stable_sequence,
    sustained_progress,
    telemetry_address_from_map,
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
        rendered = snapshot.format(0x128F8)
        self.assertIn("LED          ON", rendered)
        self.assertIn("QUEUE_RECV", rendered)
        self.assertIn("Errors       0", rendered)

    def test_odd_sequence_is_unstable(self) -> None:
        self.assertFalse(stable_sequence(101))
        self.assertTrue(stable_sequence(102))

    def test_sequence_is_little_endian(self) -> None:
        self.assertEqual(decode_sequence(b"\x34\x12"), 0x1234)

    def test_map_symbol_resolves_to_physical_address(self) -> None:
        text = "126A:0258+     _gRp86Telemetry\n"
        self.assertEqual(telemetry_address_from_map(text), 0x128F8)

    def test_stable_reader_retries_odd_sequence(self) -> None:
        stable = struct.pack("<8H", 1, 9, 7, 7, 12, 5, 7, 0)
        responses = iter((b"\x0b\x00", b"\x0c\x00", stable, b"\x0c\x00"))

        def read_memory(_address: int, _length: int) -> bytes:
            return next(responses)

        snapshot = read_stable_telemetry(read_memory, 0x128F8)
        self.assertEqual(snapshot.event_sequence, 12)
        self.assertEqual(snapshot.queue_rx_count, 7)

    def test_progress_witness_requires_all_core_counters(self) -> None:
        first = FreeRTOSSystemTelemetry(0, 10, 20, 20, 100, 5, 20, 0)
        last = FreeRTOSSystemTelemetry(1, 12, 24, 24, 112, 2, 12, 0)
        passed, failures = sustained_progress(first, last)
        self.assertTrue(passed)
        self.assertEqual(failures, ())

        stalled = FreeRTOSSystemTelemetry(0, 10, 20, 20, 100, 5, 20, 0)
        passed, failures = sustained_progress(first, stalled)
        self.assertFalse(passed)
        self.assertIn("LED toggle count did not advance", failures)
        self.assertIn("queue TX count did not advance", failures)

    def test_counter_delta_wraps_as_u16(self) -> None:
        self.assertEqual(counter_delta(0xFFFE, 1), 3)

    def test_invalid_sizes_and_missing_symbol_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "exactly 16 bytes"):
            FreeRTOSSystemTelemetry.decode(b"\x00" * 15)
        with self.assertRaisesRegex(ValueError, "exactly 2 bytes"):
            decode_sequence(b"\x00")
        with self.assertRaisesRegex(ValueError, "_gRp86Telemetry"):
            telemetry_address_from_map("no symbols here")
        with self.assertRaisesRegex(ValueError, "at least 1"):
            read_stable_telemetry(lambda _a, _l: b"", 0, attempts=0)


if __name__ == "__main__":
    unittest.main()
