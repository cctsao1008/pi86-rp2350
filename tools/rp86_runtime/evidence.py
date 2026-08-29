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


RP86_RUNTIME = ValidationProfile(
    name="rp86-runtime",
    filename_prefix="rp86_runtime",
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
        _line("IRQ commit count", r"IRQ mailbox commits\s+=\s+[1-9][0-9]*"),
        _line("EOI count", r"Native EOI writes\s+=\s+[1-9][0-9]*"),
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
        _line("runtime result", r"RP86 RUNTIME RESULT\s+=\s+PASS"),
        _line(
            "persistent electrical state",
            r"Physical processor remains active in STI/HLT; RESET is not asserted\.",
        ),
    ),
)

PROFILES = {RP86_RUNTIME.name: RP86_RUNTIME}
FAIL_TOKEN = re.compile(r"(?<![A-Za-z0-9_])(FAIL|INVALID)(?![A-Za-z0-9_])")


def normalize_output(text: str) -> str:
    """Normalize terminal line endings without changing evidence fields."""

    return text.replace("\r\n", "\n").replace("\r", "\n").lstrip("\ufeff")


def validate_output(text: str, profile: ValidationProfile = RP86_RUNTIME) -> ValidationReport:
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
    """Explain accepted physical-runtime evidence from named checks only."""

    passed = set(report.passed_checks)
    story: list[str] = []
    if {"software INT60", "first INTA count", "second INTA count"} <= passed:
        story.append(
            "The physical processor completed native INT 60h and accepted all "
            "RP2350 interrupt requests through real two-cycle INTA handshakes."
        )
    if {"IRQ mailbox commit", "native EOI", "heartbeat active"} <= passed:
        story.append(
            "Each interrupt reached the native ISR, committed its reply, issued "
            "EOI, and returned to STI/HLT."
        )
    if {"PIO allocation", "current-cycle M33"} <= passed:
        story.append(
            "Prepared PIO/DMA streams served current bus cycles without an M33 "
            "software callback."
        )
    if {"persistent processor state", "persistent electrical state"} <= passed:
        story.append(
            "The physical processor remains alive in STI/HLT with its interrupt "
            "heartbeat armed."
        )
    if report.errors:
        story.append("Physical evidence is incomplete or incorrect; acceptance failed.")
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
    configured = os.environ.get("RP86_VALIDATION_LOG_DIR")
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
        description="capture or revalidate RP86 physical-runtime evidence"
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Windows COM port, for example COM7")
    source.add_argument("--input", type=Path, help="revalidate an existing CDC log")
    source.add_argument(
        "--list-ports", action="store_true", help="list serial ports and exit"
    )
    parser.add_argument("--profile", choices=PROFILES, default=RP86_RUNTIME.name)
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
