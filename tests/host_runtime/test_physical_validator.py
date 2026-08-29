import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.evidence import (  # noqa: E402
    RP86_RUNTIME,
    explain_output,
    validate_output,
)


class PhysicalValidatorTests(unittest.TestCase):
    RUNTIME_OUTPUT = """[PERSISTENT RP86 RUNTIME]
RESET clock qualification = PASS
Software INT 60h commit    = PASS
Physical INTR assertions   = 8
INTA #1 accepts            = 8 PASS
INTA #2 completions        = 8 PASS
IRQ mailbox commit         = PASS
Native EOI                 = PASS
Heartbeat active           = PASS
Native processor identity  = INTEL 8086 (AAD16=0012) PASS
IRQ mailbox commits        = 8
Native EOI writes          = 8
PIO1 non-AD isolation      = PASS
Observer complete cycles   = 256
PIO1 allocation            = SM0 RESET+INT60, SM1 IRQ ROM, SM2 IRQ I/O, SM3 INTA
PIO instruction words      = 22 + 10 = 32/32
Current-cycle M33          = NONE
Processor runtime state    = STI/HLT idle; IRQ heartbeat remains armed
RP86 RUNTIME RESULT        = PASS
Physical processor remains active in STI/HLT; RESET is not asserted.
"""

    def test_accepted_runtime_evidence_passes(self) -> None:
        report = validate_output(self.RUNTIME_OUTPUT)
        self.assertTrue(report.passed, report.errors)

    def test_crlf_capture_passes(self) -> None:
        report = validate_output(self.RUNTIME_OUTPUT.replace("\n", "\r\n"))
        self.assertTrue(report.passed, report.errors)

    def test_any_fail_token_is_rejected(self) -> None:
        report = validate_output(self.RUNTIME_OUTPUT.replace("PASS", "FAIL", 1))
        self.assertFalse(report.passed)
        self.assertTrue(any("failure token" in error for error in report.errors))

    def test_missing_terminal_state_is_rejected(self) -> None:
        report = validate_output(
            self.RUNTIME_OUTPUT.replace(RP86_RUNTIME.end_marker, "")
        )
        self.assertFalse(report.passed)
        self.assertIn(
            "missing or incorrect field: persistent electrical state",
            report.errors,
        )

    def test_missing_native_identity_is_rejected(self) -> None:
        report = validate_output(
            self.RUNTIME_OUTPUT.replace(
                "Native processor identity  = INTEL 8086 (AAD16=0012) PASS",
                "Native processor identity  = UNKNOWN",
            )
        )
        self.assertFalse(report.passed)
        self.assertIn(
            "missing or incorrect field: native processor identity",
            report.errors,
        )

    def test_explanation_is_derived_from_named_checks(self) -> None:
        report = validate_output(self.RUNTIME_OUTPUT)
        story = explain_output(self.RUNTIME_OUTPUT, report)
        self.assertTrue(any("two-cycle INTA" in sentence for sentence in story))
        self.assertTrue(any("remains alive" in sentence for sentence in story))


if __name__ == "__main__":
    unittest.main()
