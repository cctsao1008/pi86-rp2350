import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"


class FirmwareSourceStructureTests(unittest.TestCase):
    def test_production_sources_never_include_c_files(self):
        include_c = re.compile(r'^\s*#\s*include\s+"[^"]+\.c"', re.MULTILINE)
        offenders = []
        for path in sorted(FIRMWARE.rglob("*")):
            if path.suffix not in {".c", ".h"}:
                continue
            if include_c.search(path.read_text(encoding="utf-8")):
                offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual([], offenders)

    def test_entry_point_and_runtime_policy_are_separate(self):
        main_source = (FIRMWARE / "main.c").read_text(encoding="utf-8")
        self.assertIn("rp86_canonical_runtime_run", main_source)
        self.assertLessEqual(len(main_source.splitlines()), 20)
        self.assertTrue((FIRMWARE / "runtime" / "canonical_runtime.c").is_file())
        self.assertTrue((FIRMWARE / "bus" / "prepared_responder.c").is_file())

    def test_superseded_parallel_runtime_is_absent(self):
        self.assertFalse((FIRMWARE / "runtime" / "runtime.c").exists())
        self.assertFalse((FIRMWARE / "runtime" / "runtime.h").exists())


if __name__ == "__main__":
    unittest.main()
