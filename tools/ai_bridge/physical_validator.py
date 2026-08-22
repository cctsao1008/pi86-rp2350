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


PROFILES = {AI_B1_A.name: AI_B1_A}
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


def capture_port(port: str, baud: int, timeout: float, end_marker: str) -> bytes:
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
            raw = capture_port(args.port, args.baud, args.timeout, profile.end_marker)
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
    print_report(report)
    return PASS_EXIT if report.passed else VALIDATION_EXIT


if __name__ == "__main__":
    raise SystemExit(main())
