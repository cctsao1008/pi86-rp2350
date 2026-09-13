import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from package_workload import build_package  # noqa: E402
from rp86_runtime.workload import (  # noqa: E402
    FLAG_CLOCK_STEPPED,
    FLAG_PERSISTENT,
    FLAG_SHARED_MEMORY,
    decode_workload_file,
)


class WorkloadPackageTests(unittest.TestCase):
    def test_metadata_builds_crc_protected_package(self) -> None:
        image = bytes.fromhex("90f4")
        metadata = {
            "format": "p86w-v1",
            "name": "test",
            "load_address": "0x10000",
            "entry": "1000:0000",
            "stack": "3000:FFFE",
            "clock": "clock-stepped",
            "flags": ["persistent"],
            "shared_memory": {"base": "0x3F000", "size": "0x1000"},
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metadata_path = root / "workload.json"
            image_path = root / "workload.bin"
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            image_path.write_bytes(image)
            manifest, decoded = decode_workload_file(
                build_package(metadata_path, image_path)
            )

        self.assertEqual(decoded, image)
        self.assertEqual(manifest.load_address, 0x10000)
        self.assertEqual((manifest.entry_segment, manifest.entry_offset), (0x1000, 0))
        self.assertEqual((manifest.stack_segment, manifest.stack_offset), (0x3000, 0xFFFE))
        self.assertEqual((manifest.shared_base, manifest.shared_size), (0x3F000, 0x1000))
        self.assertEqual(
            manifest.flags,
            FLAG_CLOCK_STEPPED | FLAG_PERSISTENT | FLAG_SHARED_MEMORY,
        )

    def test_unknown_flag_is_rejected(self) -> None:
        metadata = {
            "format": "p86w-v1",
            "name": "test",
            "load_address": "0x10000",
            "entry": "1000:0000",
            "flags": ["magic"],
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metadata_path = root / "workload.json"
            image_path = root / "workload.bin"
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            image_path.write_bytes(b"\x90")
            with self.assertRaisesRegex(ValueError, "unknown workload flag"):
                build_package(metadata_path, image_path)


if __name__ == "__main__":
    unittest.main()
