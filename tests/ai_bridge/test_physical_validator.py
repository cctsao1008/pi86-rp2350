from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "ai_bridge"
sys.path.insert(0, str(TOOLS))

from physical_validator import (  # noqa: E402
    AI_B1_A,
    AI_B1_B,
    AI_B2_HID,
    COMPANION_RUNTIME,
    explain_output,
    normalize_output,
    validate_output,
)
from protocol import Message, TYPE_HELLO  # noqa: E402


class PhysicalValidatorTests(unittest.TestCase):
    COMPANION_OUTPUT = """[PERSISTENT COMPANION RUNTIME]
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
COMPANION RUNTIME RESULT   = PASS
Physical processor remains active in STI/HLT; RESET is not asserted.
"""

    HID_OUTPUT = """[HOST MAILBOX INPUT]
HELLO NEC V30
[V30 MAILBOX OUTPUT]
HELLO OPENAI CODEX
[SUMMARY]
Measurement epoch          PASS
Reset / FFFF0 fetch        PASS
First response 00EA        PASS
F0000 ROM execution        PASS
Windows HID 64-byte record PASS (sequence 1)
Core1 complete record      PASS
Core0 immutable staging    PASS
Deferred DMA reload gate   PASS (8/8 words)
V30 STATUS 00E0 transition PASS (0 -> 1)
Publication after NOT_READY PASS
Atomic DMA publication     PASS
Mailbox RX I/O 00E4        PASS (7/7 words)
V30 input XOR at 00E8      PASS
Mailbox TX I/O 00E2        PASS
Mailbox commit I/O 00E6    PASS
HID reply 64-byte record   PASS (64/64 bytes)
CDC validation log role    RECEIVE-ONLY PASS
ROM/mailbox key collisions 0 PASS
Current-cycle M33          NONE
USB IRQ during V30 epoch   MASKED PASS
Bus ownership/safety       PASS
AI-B2-HID RESULT           PASS
[ENGINEERING DETAILS]
AI-B2-HID Composite Mailbox - 0.600 MHz
Host transport             = Windows USB HID 64-byte record; CDC log only
USB identity               = VID CAFE PID 4011
PIO instruction words      = 12 + 13 = 25/32
PIO1 non-AD isolation      = PASS
STATUS observations        = 2 (first 0000, second 0001)
ROM qualified pairs        = 121/121 PASS
Mailbox qualified pairs    = 9/9 PASS
Mailbox DMA live pre/post  = key 0/0 response 0/0
Mailbox DMA reload count   = key 8 response 8 PASS
Response deadline misses   = 0 PASS
ROM image                  = 230 bytes; SHA-256 4fceb34847a713477ce45e4b23a06770d212044f5704154e35b4d94ab1701cb4
TERMINAL SAFE STATE        = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
"""

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

    def test_ai_b1b_profile_sends_canonical_binary_record(self) -> None:
        self.assertIsNotNone(AI_B1_B.request)
        request = Message.decode(AI_B1_B.request)
        self.assertEqual(request.message_type, TYPE_HELLO)
        self.assertEqual(request.sequence, 1)
        self.assertEqual(request.payload, b"HELLO NEC V30")

    def test_ai_b2_hid_composite_profile_passes(self) -> None:
        report = validate_output(self.HID_OUTPUT, AI_B2_HID)
        self.assertTrue(report.passed, report.errors)
        story = explain_output(self.HID_OUTPUT, report)
        self.assertTrue(any("through HID" in sentence for sentence in story))
        self.assertTrue(any("seven mailbox words" in sentence for sentence in story))
        self.assertTrue(any("only the multiplexed AD pins" in sentence for sentence in story))
        self.assertTrue(any("AD bus high-Z" in sentence for sentence in story))

    def test_persistent_companion_profile_passes(self) -> None:
        report = validate_output(self.COMPANION_OUTPUT, COMPANION_RUNTIME)
        self.assertTrue(report.passed, report.errors)
        story = explain_output(self.COMPANION_OUTPUT, report)
        self.assertTrue(any("two-cycle INTA" in sentence for sentence in story))
        self.assertTrue(any("remains alive" in sentence for sentence in story))

    def test_committed_ai_b2_hid_evidence_passes(self) -> None:
        evidence = (
            ROOT
            / "docs"
            / "validation"
            / "evidence"
            / "ai_b2_hid_20260823_171808+0800.log"
        ).read_text(encoding="utf-8")
        report = validate_output(evidence, AI_B2_HID)
        self.assertTrue(report.passed, report.errors)

    def test_ai_b2_hid_rejects_cdc_command_transport(self) -> None:
        corrupted = self.HID_OUTPUT.replace(
            "Windows USB HID 64-byte record; CDC log only",
            "Windows USB CDC binary record",
        )
        report = validate_output(corrupted, AI_B2_HID)
        self.assertFalse(report.passed)
        self.assertIn("missing or incorrect field: host transport identity", report.errors)

    def test_ai_b2_hid_rejects_partial_hid_reply(self) -> None:
        corrupted = self.HID_OUTPUT.replace("PASS (64/64 bytes)", "FAIL (32/64 bytes)")
        report = validate_output(corrupted, AI_B2_HID)
        self.assertFalse(report.passed)
        self.assertTrue(any("HID reply record" in error for error in report.errors))

    def test_ai_b2_hid_rejects_non_ad_isolation_failure(self) -> None:
        corrupted = self.HID_OUTPUT.replace(
            "PIO1 non-AD isolation      = PASS",
            "PIO1 non-AD isolation      = FAIL",
        )
        report = validate_output(corrupted, AI_B2_HID)
        self.assertFalse(report.passed)
        self.assertIn(
            "missing or incorrect field: PIO1 non-AD isolation", report.errors
        )


if __name__ == "__main__":
    unittest.main()
