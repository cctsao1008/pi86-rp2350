from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "runtime"))

from rp86_runtime.protocol import (  # noqa: E402
    Message,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_COMMIT,
    TYPE_WORKLOAD_DATA,
)
from rp86_runtime.workload import (  # noqa: E402
    DATA_BYTES,
    FLAG_SHARED_MEMORY,
    FLAG_STDIO,
    FLAG_CLOCK_STEPPED,
    MANIFEST_SIZE,
    WorkloadManifest,
    decode_data_payload,
    decode_workload_file,
    encode_workload_file,
    control_record,
    parse_far_pointer,
    upload_records,
    workload_from_command,
    workload_from_bytes,
    decode_status_payload,
)


class WorkloadTests(unittest.TestCase):
    def sample(self) -> tuple[WorkloadManifest, bytes]:
        image = bytes(range(1, 138))
        manifest = WorkloadManifest.for_image(
            image,
            load_address=0x20000,
            entry_segment=0x2000,
            entry_offset=0,
            stack_segment=0x3000,
            stack_offset=0xFFFE,
            shared_base=0x40000,
            shared_size=0x1000,
            flags=FLAG_STDIO | FLAG_SHARED_MEMORY,
        )
        return manifest, image

    def test_manifest_is_fixed_and_round_trips(self) -> None:
        manifest, image = self.sample()
        self.assertEqual(len(manifest.encode()), MANIFEST_SIZE)
        decoded_manifest, decoded_image = decode_workload_file(
            encode_workload_file(manifest, image)
        )
        self.assertEqual(decoded_manifest, manifest)
        self.assertEqual(decoded_image, image)

    def test_entry_must_be_inside_loaded_image(self) -> None:
        with self.assertRaisesRegex(ValueError, "entry point"):
            WorkloadManifest.for_image(
                b"\x90\x90",
                load_address=0x20000,
                entry_segment=0x3000,
                entry_offset=0,
            )

    def test_image_must_fit_physical_address_space(self) -> None:
        with self.assertRaisesRegex(ValueError, "address-space limit"):
            WorkloadManifest.for_image(
                b"\x90" * 32,
                load_address=0xFFFF0,
                entry_segment=0xFFFF,
                entry_offset=0,
            )

    def test_shared_range_requires_explicit_flag(self) -> None:
        with self.assertRaisesRegex(ValueError, "FLAG_SHARED_MEMORY"):
            WorkloadManifest.for_image(
                b"\x90\x90",
                load_address=0x20000,
                entry_segment=0x2000,
                entry_offset=0,
                shared_base=0x30000,
                shared_size=0x100,
            )

    def test_upload_uses_fixed_64_byte_records(self) -> None:
        manifest, image = self.sample()
        records = upload_records(
            manifest, image, transfer_id=0x12345678, first_sequence=10
        )
        self.assertEqual(records[0].message_type, TYPE_WORKLOAD_BEGIN)
        self.assertEqual(records[-1].message_type, TYPE_WORKLOAD_COMMIT)
        self.assertTrue(all(len(record.encode()) == 64 for record in records))
        chunks: list[bytes] = []
        for record in records[1:-1]:
            self.assertEqual(record.message_type, TYPE_WORKLOAD_DATA)
            transfer_id, offset, data = decode_data_payload(record.payload)
            self.assertEqual(transfer_id, 0x12345678)
            self.assertEqual(offset, len(b"".join(chunks)))
            self.assertLessEqual(len(data), DATA_BYTES)
            chunks.append(data)
            self.assertEqual(Message.decode(record.encode()), record)
        self.assertEqual(b"".join(chunks), image)

    def test_crc_rejects_mutated_image(self) -> None:
        manifest, image = self.sample()
        encoded = bytearray(encode_workload_file(manifest, image))
        encoded[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            decode_workload_file(bytes(encoded))

    def test_flash_package_bytes_preserve_manifest_and_crc(self) -> None:
        manifest, image = self.sample()
        encoded = encode_workload_file(manifest, image)
        decoded, decoded_image, records = workload_from_bytes(
            encoded,
            ("flash:/CALC.P86W",),
            transfer_id=0x55AA,
            first_sequence=50,
        )
        self.assertEqual(decoded, manifest)
        self.assertEqual(decoded_image, image)
        self.assertEqual(records[0].sequence, 50)

    def test_flash_package_bytes_reject_crc_mismatch(self) -> None:
        manifest, image = self.sample()
        encoded = bytearray(encode_workload_file(manifest, image))
        encoded[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            workload_from_bytes(
                bytes(encoded),
                ("flash:/CALC.P86W",),
                transfer_id=0x55AA,
                first_sequence=50,
            )

    def test_control_record_uses_current_workload_zero(self) -> None:
        record = control_record("run", workload_id=0, sequence=22)
        self.assertEqual(record.sequence, 22)
        self.assertEqual(record.payload, b"\x01\0\0\0\0\0\0\0")

    def test_far_pointer_is_hexadecimal(self) -> None:
        self.assertEqual(parse_far_pointer("1000:000a"), (0x1000, 0x000A))

    def test_flat_load_command_builds_manifest(self) -> None:
        path = ROOT / "tests" / "runtime" / "sample_workload.bin"
        path.write_bytes(b"\x90\xeb\xfd")
        try:
            manifest, image, records = workload_from_command(
                (str(path), "--address", "0x10000", "--entry", "1000:0000"),
                transfer_id=7,
                first_sequence=30,
            )
        finally:
            path.unlink(missing_ok=True)
        self.assertEqual(image, b"\x90\xeb\xfd")
        self.assertEqual(manifest.load_address, 0x10000)
        self.assertEqual((manifest.entry_segment, manifest.entry_offset), (0x1000, 0))
        self.assertEqual(records[0].sequence, 30)

    def test_flat_load_can_request_clock_stepped_execution(self) -> None:
        path = ROOT / "tests" / "runtime" / "sample_clock_workload.bin"
        path.write_bytes(b"\x90\xf4")
        try:
            manifest, _image, _records = workload_from_command(
                (str(path), "--clock", "clock-stepped"),
                transfer_id=8,
                first_sequence=40,
            )
        finally:
            path.unlink(missing_ok=True)
        self.assertTrue(manifest.flags & FLAG_CLOCK_STEPPED)

    def test_legacy_status_payload_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "exactly 52 bytes"):
            decode_status_payload(bytes(24))


if __name__ == "__main__":
    unittest.main()
