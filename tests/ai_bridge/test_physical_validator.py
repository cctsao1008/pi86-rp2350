from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "ai_bridge"
sys.path.insert(0, str(TOOLS))

from physical_validator import AI_B1_A, normalize_output, validate_output  # noqa: E402


class PhysicalValidatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        evidence = (
            ROOT
            / "docs"
            / "validation"
            / "ai_b1a_runtime_mailbox_600khz_validation.md"
        ).read_text(encoding="utf-8")
        match = re.search(r"## Complete physical output\s+```text\n(.*?)\n```", evidence, re.S)
        if match is None:
            raise AssertionError("accepted AI-B1-A evidence block is missing")
        cls.accepted_output = match.group(1)

    def test_accepted_physical_evidence_passes(self) -> None:
        report = validate_output(self.accepted_output)
        self.assertTrue(report.passed, report.errors)

    def test_crlf_capture_passes(self) -> None:
        report = validate_output(self.accepted_output.replace("\n", "\r\n"))
        self.assertTrue(report.passed, report.errors)

    def test_any_fail_token_is_rejected(self) -> None:
        corrupted = self.accepted_output.replace(
            "Response deadline misses   = 0 PASS",
            "Response deadline misses   = 1 FAIL",
        )
        report = validate_output(corrupted)
        self.assertFalse(report.passed)
        self.assertTrue(any("failure token" in item for item in report.errors))

    def test_wrong_clock_is_rejected(self) -> None:
        corrupted = self.accepted_output.replace("0.600 MHz", "0.200 MHz")
        report = validate_output(corrupted)
        self.assertFalse(report.passed)
        self.assertIn("missing or incorrect field: clock and engine identity", report.errors)

    def test_missing_terminal_state_is_rejected(self) -> None:
        corrupted = self.accepted_output.replace(AI_B1_A.end_marker, "")
        report = validate_output(corrupted)
        self.assertFalse(report.passed)
        self.assertIn("missing or incorrect field: terminal electrical state", report.errors)


if __name__ == "__main__":
    unittest.main()
