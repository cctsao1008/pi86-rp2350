#!/usr/bin/env python3
"""Capture and validate pi86-rp2350 physical evidence on Windows.

The validator deliberately keeps transport and acceptance policy separate.
Serial support is imported only for live capture, so saved evidence can be
revalidated with the Python standard library alone.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import os
from pathlib import Path
import re
import sys
import time
from typing import Iterable, Pattern

from .protocol import Message, TYPE_HELLO


PASS_EXIT = 0
SERIAL_EXIT = 3
CAPTURE_EXIT = 4
VALIDATION_EXIT = 5


@dataclass(frozen=True)
class Check:
    name: str
    pattern: Pattern[str]


@dataclass(frozen=True)
class ValidationProfile:
    name: str
    filename_prefix: str
    end_marker: str
    checks: tuple[Check, ...]
    request: bytes | None = None


@dataclass(frozen=True)
class ValidationReport:
    profile: str
    passed: bool
    passed_checks: tuple[str, ...]
    errors: tuple[str, ...]


def _line(name: str, expression: str) -> Check:
    return Check(name, re.compile(rf"(?m)^{expression}\s*$"))


AI_B1_A = ValidationProfile(
    name="ai-b1-a",
    filename_prefix="ai_b1a",
    end_marker="CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.",
    checks=(
        _line("V30 reply", r"HELLO OPENAI CODEX"),
        _line("measurement epoch", r"Measurement epoch\s+PASS"),
        _line("reset vector", r"Reset / FFFF0 fetch\s+PASS"),
        _line("first response", r"First response 00EA\s+PASS"),
        _line("ROM execution", r"F0000 ROM execution\s+PASS"),
        _line("Core1 complete record", r"Core1 complete record\s+PASS"),
        _line("Core0 immutable staging", r"Core0 immutable staging\s+PASS"),
        _line("mailbox RX", r"Mailbox RX I/O 00E4\s+PASS \(7/7 words\)"),
        _line("V30 XOR witness", r"V30 input XOR at 00E8\s+PASS"),
        _line("mailbox TX", r"Mailbox TX I/O 00E2\s+PASS"),
        _line("mailbox commit", r"Mailbox commit I/O 00E6\s+PASS"),
        _line("key collision gate", r"ROM/mailbox key collisions\s+0 PASS"),
        _line("current-cycle M33", r"Current-cycle M33\s+NONE"),
        _line("bus safety", r"Bus ownership/safety\s+PASS"),
        _line("AI-B1-A result", r"AI-B1-A RESULT\s+PASS"),
        _line(
            "clock and engine identity",
            r"AI-B1-A Dual-Sequence Runtime Mailbox - 0\.600 MHz",
        ),
        _line("ROM qualified pairs", r"ROM qualified pairs\s+=\s+48/48 PASS"),
        _line(
            "mailbox qualified pairs",
            r"Mailbox qualified pairs\s+=\s+7/7 PASS",
        ),
        _line(
            "ROM DMA drained",
            r"ROM DMA remain key/response\s*=\s*0/0",
        ),
        _line(
            "mailbox DMA drained",
            r"Mailbox DMA remain key/resp\s*=\s*0/0",
        ),
        _line("deadline gate", r"Response deadline misses\s+=\s+0 PASS"),
        _line(
            "ROM identity",
            r"ROM image\s+=\s+84 bytes; SHA-256 "
            r"6c203b2082b24d12a95b0443e63dc4ba65faeef5985c48e536af7d08e73e70af",
        ),
        _line("terminal safe state", r"TERMINAL SAFE STATE\s+=\s+PASS"),
        _line(
            "terminal electrical state",
            r"CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z\.",
        ),
    ),
)


AI_B1_B = ValidationProfile(
    name="ai-b1-b",
    filename_prefix="ai_b1b",
    end_marker="CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.",
    checks=(
        _line("host greeting", r"HELLO NEC V30"),
        _line("V30 reply", r"HELLO OPENAI CODEX"),
        _line("measurement epoch", r"Measurement epoch\s+PASS"),
        _line("reset vector", r"Reset / FFFF0 fetch\s+PASS"),
        _line("first response", r"First response 00EA\s+PASS"),
        _line("ROM execution", r"F0000 ROM execution\s+PASS"),
        _line(
            "Windows binary record",
            r"Windows 64-byte record\s+PASS \(sequence 1\)",
        ),
        _line("Core1 complete record", r"Core1 complete record\s+PASS"),
        _line("Core0 immutable staging", r"Core0 immutable staging\s+PASS"),
        _line(
            "deferred DMA reload gate",
            r"Deferred DMA reload gate\s+PASS \(8/8 words\)",
        ),
        _line(
            "STATUS transition",
            r"V30 STATUS 00E0 transition\s+PASS \(0 -> 1\)",
        ),
        _line("publication ordering", r"Publication after NOT_READY\s+PASS"),
        _line("atomic publication", r"Atomic DMA publication\s+PASS"),
        _line("mailbox RX", r"Mailbox RX I/O 00E4\s+PASS \(7/7 words\)"),
        _line("V30 XOR witness", r"V30 input XOR at 00E8\s+PASS"),
        _line("mailbox TX", r"Mailbox TX I/O 00E2\s+PASS"),
        _line("mailbox commit", r"Mailbox commit I/O 00E6\s+PASS"),
        _line("key collision gate", r"ROM/mailbox key collisions\s+0 PASS"),
        _line("current-cycle M33", r"Current-cycle M33\s+NONE"),
        _line("USB IRQ isolation", r"USB IRQ during V30 epoch\s+MASKED PASS"),
        _line("bus safety", r"Bus ownership/safety\s+PASS"),
        _line("AI-B1-B result", r"AI-B1-B RESULT\s+PASS"),
        _line(
            "clock and engine identity",
            r"AI-B1-B Live Mailbox Publication - 0\.600 MHz",
        ),
        _line(
            "host transport identity",
            r"Host transport\s+=\s+Windows USB CDC binary record",
        ),
        _line(
            "PIO instruction budget",
            r"PIO instruction words\s+=\s+12 \+ 13 = 25/32",
        ),
        _line(
            "STATUS physical observations",
            r"STATUS observations\s+=\s+2 \(first 0000, second 0001\)",
        ),
        _line("ROM qualified pairs", r"ROM qualified pairs\s+=\s+121/121 PASS"),
        _line(
            "mailbox qualified pairs",
            r"Mailbox qualified pairs\s+=\s+9/9 PASS",
        ),
        _line(
            "mailbox DMA live count",
            r"Mailbox DMA live pre/post\s+=\s+key 0/0 response 0/0",
        ),
        _line(
            "mailbox DMA reload count",
            r"Mailbox DMA reload count\s+=\s+key 8 response 8 PASS",
        ),
        _line("deadline gate", r"Response deadline misses\s+=\s+0 PASS"),
        _line(
            "ROM identity",
            r"ROM image\s+=\s+230 bytes; SHA-256 "
            r"4fceb34847a713477ce45e4b23a06770d212044f5704154e35b4d94ab1701cb4",
        ),
        _line("terminal safe state", r"TERMINAL SAFE STATE\s+=\s+PASS"),
        _line(
            "terminal electrical state",
            r"CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z\.",
        ),
    ),
    request=Message(TYPE_HELLO, 1, b"HELLO NEC V30").encode(),
)


AI_B2_HID = ValidationProfile(
    name="ai-b2-hid",
    filename_prefix="ai_b2_hid",
    end_marker="CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.",
    checks=(
        _line("host greeting", r"HELLO NEC V30"),
        _line("V30 reply", r"HELLO OPENAI CODEX"),
        _line("measurement epoch", r"Measurement epoch\s+PASS"),
        _line("reset vector", r"Reset / FFFF0 fetch\s+PASS"),
        _line("first response", r"First response 00EA\s+PASS"),
        _line("ROM execution", r"F0000 ROM execution\s+PASS"),
        _line(
            "Windows HID binary record",
            r"Windows HID 64-byte record\s+PASS \(sequence [0-9]+\)",
        ),
        _line("Core1 complete record", r"Core1 complete record\s+PASS"),
        _line("Core0 immutable staging", r"Core0 immutable staging\s+PASS"),
        _line(
            "deferred DMA reload gate",
            r"Deferred DMA reload gate\s+PASS \(8/8 words\)",
        ),
        _line(
            "STATUS transition",
            r"V30 STATUS 00E0 transition\s+PASS \(0 -> 1\)",
        ),
        _line("publication ordering", r"Publication after NOT_READY\s+PASS"),
        _line("atomic publication", r"Atomic DMA publication\s+PASS"),
        _line("mailbox RX", r"Mailbox RX I/O 00E4\s+PASS \(7/7 words\)"),
        _line("V30 XOR witness", r"V30 input XOR at 00E8\s+PASS"),
        _line("mailbox TX", r"Mailbox TX I/O 00E2\s+PASS"),
        _line("mailbox commit", r"Mailbox commit I/O 00E6\s+PASS"),
        _line(
            "HID reply record",
            r"HID reply 64-byte record\s+PASS \(64/64 bytes\)",
        ),
        _line(
            "CDC receive-only role",
            r"CDC validation log role\s+RECEIVE-ONLY PASS",
        ),
        _line("key collision gate", r"ROM/mailbox key collisions\s+0 PASS"),
        _line("current-cycle M33", r"Current-cycle M33\s+NONE"),
        _line("USB IRQ isolation", r"USB IRQ during V30 epoch\s+MASKED PASS"),
        _line("bus safety", r"Bus ownership/safety\s+PASS"),
        _line("AI-B2-HID result", r"AI-B2-HID RESULT\s+PASS"),
        _line(
            "clock and engine identity",
            r"AI-B2-HID Composite Mailbox - 0\.600 MHz",
        ),
        _line(
            "host transport identity",
            r"Host transport\s+=\s+Windows USB HID 64-byte record; CDC log only",
        ),
        _line("USB development identity", r"USB identity\s+=\s+VID CAFE PID 4011"),
        _line(
            "PIO instruction budget",
            r"PIO instruction words\s+=\s+12 \+ 13 = 25/32",
        ),
        _line(
            "PIO1 non-AD isolation",
            r"PIO1 non-AD isolation\s+=\s+PASS",
        ),
        _line(
            "STATUS physical observations",
            r"STATUS observations\s+=\s+2 \(first 0000, second 0001\)",
        ),
        _line("ROM qualified pairs", r"ROM qualified pairs\s+=\s+121/121 PASS"),
        _line("mailbox qualified pairs", r"Mailbox qualified pairs\s+=\s+9/9 PASS"),
        _line(
            "mailbox DMA live count",
            r"Mailbox DMA live pre/post\s+=\s+key 0/0 response 0/0",
        ),
        _line(
            "mailbox DMA reload count",
            r"Mailbox DMA reload count\s+=\s+key 8 response 8 PASS",
        ),
        _line("deadline gate", r"Response deadline misses\s+=\s+0 PASS"),
        _line(
            "ROM identity",
            r"ROM image\s+=\s+230 bytes; SHA-256 "
            r"4fceb34847a713477ce45e4b23a06770d212044f5704154e35b4d94ab1701cb4",
        ),
        _line("terminal safe state", r"TERMINAL SAFE STATE\s+=\s+PASS"),
        _line(
            "terminal electrical state",
            r"CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z\.",
        ),
    ),
)


COMPANION_RUNTIME = ValidationProfile(
    name="companion-runtime",
    filename_prefix="companion_runtime",
    end_marker="Physical processor remains active in STI/HLT; RESET is not asserted.",
    checks=(
        _line("reset qualification", r"RESET clock qualification\s+=\s+PASS"),
        _line("software INT60", r"Software INT 60h commit\s+=\s+PASS"),
        _line("physical INTR count", r"Physical INTR assertions\s+=\s+8"),
        _line("first INTA count", r"INTA #1 accepts\s+=\s+8 PASS"),
        _line("second INTA count", r"INTA #2 completions\s+=\s+8 PASS"),
        _line("IRQ mailbox commit", r"IRQ mailbox commit\s+=\s+PASS"),
        _line("native EOI", r"Native EOI\s+=\s+PASS"),
        _line("heartbeat active", r"Heartbeat active\s+=\s+PASS"),
        _line(
            "native processor identity",
            r"Native processor identity\s+=\s+(?:INTEL 8086 \(AAD16=0012\)|"
            r"NEC V30 \(AAD16=000C\)) PASS",
        ),
        _line("IRQ commit count", r"IRQ mailbox commits\s+=\s+(?:[2-9]|[1-9][0-9]+)"),
        _line("EOI count", r"Native EOI writes\s+=\s+(?:[2-9]|[1-9][0-9]+)"),
        _line("PIO1 non-AD isolation", r"PIO1 non-AD isolation\s+=\s+PASS"),
        _line("observer activity", r"Observer complete cycles\s+=\s+[1-9][0-9]*"),
        _line(
            "PIO allocation",
            r"PIO1 allocation\s+=\s+SM0 RESET\+INT60, SM1 IRQ ROM, "
            r"SM2 IRQ I/O, SM3 INTA",
        ),
        _line("PIO instruction budget", r"PIO instruction words\s+=\s+22 \+ 10 = 32/32"),
        _line("current-cycle M33", r"Current-cycle M33\s+=\s+NONE"),
        _line(
            "persistent processor state",
            r"Processor runtime state\s+=\s+STI/HLT idle; IRQ heartbeat remains armed",
        ),
        _line("companion result", r"COMPANION RUNTIME RESULT\s+=\s+PASS"),
        _line(
            "persistent electrical state",
            r"Physical processor remains active in STI/HLT; RESET is not asserted\.",
        ),
    ),
)

PROFILES = {
    profile.name: profile
    for profile in (AI_B1_A, AI_B1_B, AI_B2_HID, COMPANION_RUNTIME)
}
FAIL_TOKEN = re.compile(r"(?<![A-Za-z0-9_])(FAIL|INVALID)(?![A-Za-z0-9_])")


def normalize_output(text: str) -> str:
    """Normalize terminal line endings without changing evidence fields."""

    return text.replace("\r\n", "\n").replace("\r", "\n").lstrip("\ufeff")


def validate_output(text: str, profile: ValidationProfile = AI_B1_A) -> ValidationReport:
    """Apply one physical acceptance profile to captured CDC output."""

    normalized = normalize_output(text)
    passed: list[str] = []
    errors: list[str] = []

    bad_tokens = sorted(set(FAIL_TOKEN.findall(normalized)))
    if bad_tokens:
        errors.append("failure token present: " + ", ".join(bad_tokens))
    else:
        passed.append("no FAIL/INVALID tokens")

    for check in profile.checks:
        if check.pattern.search(normalized):
            passed.append(check.name)
        else:
            errors.append(f"missing or incorrect field: {check.name}")

    return ValidationReport(
        profile=profile.name,
        passed=not errors,
        passed_checks=tuple(passed),
        errors=tuple(errors),
    )


def explain_output(text: str, report: ValidationReport) -> tuple[str, ...]:
    """Turn a CDC transcript into a deterministic physical-execution story.

    This is deliberately not an AI summary.  Every sentence is selected from
    the named acceptance checks, so the raw CDC log remains the canonical
    evidence and the explanation is reproducible on Windows or in CI.
    """

    passed = set(report.passed_checks)
    normalized = normalize_output(text)
    story: list[str] = []

    if {"software INT60", "first INTA count", "second INTA count"} <= passed:
        story.append(
            "The physical processor completed its native INT 60h service, then accepted "
            "all eight RP2350 interrupt requests through real two-cycle INTA handshakes."
        )
    if {"IRQ mailbox commit", "native EOI", "heartbeat active"} <= passed:
        story.append(
            "Each accepted companion interrupt reached the native processor ISR, consumed "
            "the mailbox record, committed its reply, issued EOI, and returned to STI/HLT."
        )
    if {"PIO allocation", "current-cycle M33"} <= passed:
        story.append(
            "Independent PIO exact streams served foreground, IRQ ROM, and IRQ I/O "
            "cycles; M33 did not answer any current bus cycle."
        )
    if {"persistent processor state", "persistent electrical state"} <= passed:
        story.append(
            "The run did not terminate by resetting the CPU: the physical processor remains alive "
            "in STI/HLT with its interrupt heartbeat armed."
        )

    if "Windows HID binary record" in passed:
        story.append(
            "Windows delivered one complete 64-byte request through HID; "
            "CDC carried validation output only."
        )
    elif "Windows binary record" in passed:
        story.append("Windows delivered one complete 64-byte request through CDC.")

    if {"reset vector", "first response", "ROM execution"} <= passed:
        story.append(
            "The physical V30 fetched FFFF0, received opcode 00EA, and executed "
            "the internal-SRAM-backed ROM at F0000."
        )

    if {
        "Core1 complete record",
        "Core0 immutable staging",
        "STATUS transition",
        "publication ordering",
        "atomic publication",
    } <= passed:
        story.append(
            "Core1 accepted the complete host record; Core0 published immutable "
            "mailbox data only after the V30 observed NOT_READY, then STATUS became READY."
        )

    if {"mailbox RX", "V30 XOR witness", "mailbox TX", "mailbox commit"} <= passed:
        story.append(
            "The V30 physically read all seven mailbox words, consumed them, "
            "wrote its reply through port 00E2, and committed it through port 00E6."
        )

    if "HID reply record" in passed:
        story.append(
            "RP2350 returned the V30 reply to Windows as one complete 64-byte HID record."
        )

    if {"deadline gate", "key collision gate", "current-cycle M33"} <= passed:
        story.append(
            "PIO/DMA handled every qualified current bus cycle with zero deadline "
            "misses and no ROM/mailbox key collision; M33 did not answer current cycles."
        )

    if "PIO1 non-AD isolation" in passed:
        story.append(
            "Before RESET release, the PIO1 responder window was verified to own "
            "only the multiplexed AD pins and no V30 control or address input pin."
        )

    if {"bus safety", "terminal safe state", "terminal electrical state"} <= passed:
        story.append(
            "The run ended safely with RESET high, CLK low, and the multiplexed AD bus high-Z."
        )

    reply = re.search(r"(?m)^HELLO OPENAI CODEX\s*$", normalized)
    if reply is not None and "V30 reply" in passed:
        story.append('Observed V30 application reply: "HELLO OPENAI CODEX".')

    if report.errors:
        story.append(
            "CDC evidence is incomplete or incorrect; acceptance did not pass."
        )
    elif report.passed:
        story.append(
            f"All {len(report.passed_checks)} deterministic {report.profile} "
            "acceptance checks passed."
        )
    return tuple(story)


def _serial_modules():
    try:
        import serial  # type: ignore[import-not-found]
        from serial.tools import list_ports  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required for COM capture; run: py -m pip install pyserial"
        ) from exc
    return serial, list_ports


def format_ports() -> list[str]:
    """Return stable, human-readable descriptions of Windows serial ports."""

    _, list_ports = _serial_modules()
    ports = sorted(list_ports.comports(), key=lambda item: item.device)
    return [f"{p.device:8} {p.description} [{p.hwid}]" for p in ports]


def capture_port(
    port: str,
    baud: int,
    timeout: float,
    end_marker: str,
    request: bytes | None = None,
) -> bytes:
    """Capture one CDC run, tolerating a temporary USB disconnect/reconnect."""

    serial, _ = _serial_modules()
    deadline = time.monotonic() + timeout
    captured = bytearray()
    connection = None
    marker = end_marker.encode("ascii")
    announced_wait = False

    print(f"Armed on {port}; connect or reset the RP2350 if no output appears.")
    while time.monotonic() < deadline:
        if connection is None:
            try:
                connection = serial.Serial(
                    port=port,
                    baudrate=baud,
                    timeout=0.25,
                    write_timeout=1.0,
                )
                connection.dtr = True
                if request is not None and not captured:
                    written = connection.write(request)
                    connection.flush()
                    if written != len(request):
                        raise RuntimeError(
                            f"short CDC write: {written}/{len(request)} bytes"
                        )
                    print(f"Sent binary request       = {written} bytes")
                announced_wait = False
            except (serial.SerialException, OSError):
                if not announced_wait:
                    print(f"Waiting for {port}...")
                    announced_wait = True
                time.sleep(0.25)
                continue

        try:
            chunk = connection.read(max(connection.in_waiting, 1))
        except (serial.SerialException, OSError):
            connection.close()
            connection = None
            continue

        if not chunk:
            continue
        captured.extend(chunk)
        sys.stdout.write(chunk.decode("utf-8", errors="replace"))
        sys.stdout.flush()
        if marker in captured:
            break

    if connection is not None:
        connection.close()
    return bytes(captured)


def default_output_dir() -> Path:
    configured = os.environ.get("PI86_VALIDATION_LOG_DIR")
    if configured:
        return Path(configured).expanduser()
    documents = Path.home() / "Documents"
    base = documents if documents.is_dir() else Path.home()
    return base / "pi86-validation-logs"


def save_capture(raw: bytes, output_dir: Path, prefix: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S%z")
    destination = output_dir / f"{prefix}_{timestamp}.log"
    destination.write_bytes(raw)
    return destination


def print_report(report: ValidationReport) -> None:
    print("\n[PYTHON PHYSICAL VALIDATION]")
    print(f"Profile                  = {report.profile}")
    print(f"Checks passed            = {len(report.passed_checks)}")
    if report.errors:
        for error in report.errors:
            print(f"ERROR                    = {error}")
    print(f"PYTHON VALIDATION RESULT = {'PASS' if report.passed else 'FAIL'}")


def print_explanation(story: Iterable[str]) -> None:
    print("\n[CDC LOG EXPLANATION]")
    for index, sentence in enumerate(story, 1):
        print(f"{index}. {sentence}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="capture or revalidate pi86-rp2350 physical evidence"
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Windows COM port, for example COM7")
    source.add_argument("--input", type=Path, help="revalidate an existing CDC log")
    source.add_argument(
        "--list-ports", action="store_true", help="list serial ports and exit"
    )
    parser.add_argument("--profile", choices=PROFILES, default=AI_B1_A.name)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--output-dir", type=Path, default=default_output_dir())
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    profile = PROFILES[args.profile]

    if args.list_ports:
        try:
            ports = format_ports()
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return SERIAL_EXIT
        if not ports:
            print("No serial ports found.")
            return SERIAL_EXIT
        print("\n".join(ports))
        return PASS_EXIT

    if args.input is not None:
        try:
            raw = args.input.read_bytes()
        except OSError as exc:
            print(f"ERROR: cannot read {args.input}: {exc}", file=sys.stderr)
            return CAPTURE_EXIT
    else:
        try:
            raw = capture_port(
                args.port,
                args.baud,
                args.timeout,
                profile.end_marker,
                profile.request,
            )
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return SERIAL_EXIT
        saved = save_capture(raw, args.output_dir, profile.filename_prefix)
        print(f"\nRaw evidence saved       = {saved}")
        if profile.end_marker.encode("ascii") not in raw:
            print(f"ERROR: capture timed out after {args.timeout:.1f} seconds")
            return CAPTURE_EXIT

    text = raw.decode("utf-8", errors="replace")
    report = validate_output(text, profile)
    print_explanation(explain_output(text, report))
    print_report(report)
    return PASS_EXIT if report.passed else VALIDATION_EXIT


if __name__ == "__main__":
    raise SystemExit(main())
