#!/usr/bin/env python3
"""Windows HID bridge and CDC evidence collector for pi86-rp2350."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import struct
import sys
import time
from typing import Any

from host_shell import command_help, parse_command, unavailable_message

from physical_validator import (
    AI_B2_HID,
    COMPANION_RUNTIME,
    explain_output,
    validate_output,
)
from protocol import (
    MESSAGE_SIZE,
    Message,
    STATUS_OK,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    TYPE_HELLO,
    TYPE_RESULT,
    TYPE_TEXT,
)

CANONICAL_GREETING = b"HELLO NEC V30"
CANONICAL_REPLY = b"HELLO OPENAI CODEX"
HEARTBEAT_REPLY = b"V30 HEARTBEAT OK"
COMMAND_REPLY = b"V30 COMMAND OK"
USB_VID = 0xCAFE
USB_PID = 0x4011
PROCESSOR_NAMES = {
    "nec-v30": "NEC V30",
    "intel-8086": "INTEL 8086",
}
TERMINAL_MARKERS = tuple(
    profile.end_marker.encode("ascii")
    for profile in (AI_B2_HID, COMPANION_RUNTIME)
)

PASS_EXIT = 0
DEPENDENCY_EXIT = 3
TRANSPORT_EXIT = 4
VALIDATION_EXIT = 5


def heartbeat_payload(sequence: int, nonce: int | None = None) -> bytes:
    """Build the seven native V30 mailbox words for one fresh liveness proof."""
    if nonce is None:
        nonce = secrets.randbits(64)
    return struct.pack("<2sIQ", b"HB", sequence & 0xFFFFFFFF, nonce)


@dataclass
class HeartbeatStats:
    completed: int = 0
    lost: int = 0
    last_ms: float = 0.0
    minimum_ms: float = float("inf")
    maximum_ms: float = 0.0
    total_ms: float = 0.0

    def accept(self, latency_ms: float) -> None:
        self.completed += 1
        self.last_ms = latency_ms
        self.minimum_ms = min(self.minimum_ms, latency_ms)
        self.maximum_ms = max(self.maximum_ms, latency_ms)
        self.total_ms += latency_ms

    @property
    def average_ms(self) -> float:
        return self.total_ms / self.completed if self.completed else 0.0


def simulate_v30(record: bytes) -> bytes:
    request = Message.decode(record)
    if request.message_type != TYPE_HELLO or request.payload != CANONICAL_GREETING:
        raise ValueError("simulated V30 rejected the greeting")
    return Message(TYPE_TEXT, request.sequence, CANONICAL_REPLY).encode()


def hid_output_report(record: bytes) -> bytes:
    """Add HIDAPI's required zero report-ID byte to one ABI record."""
    if len(record) != MESSAGE_SIZE:
        raise ValueError(f"record must be exactly {MESSAGE_SIZE} bytes")
    return b"\0" + record


def normalize_hid_input(report: bytes) -> bytes:
    """Accept HIDAPI's platform representation, never a partial record."""
    if len(report) == MESSAGE_SIZE + 1 and report[0] == 0:
        report = report[1:]
    if len(report) != MESSAGE_SIZE:
        raise ValueError(
            f"HID reply must contain exactly {MESSAGE_SIZE} ABI bytes; got {len(report)}"
        )
    return report


def validate_reply(
    record: bytes,
    sequence: int,
    expected_type: int = TYPE_TEXT,
    expected_payload: bytes = CANONICAL_REPLY,
) -> Message:
    reply = Message.decode(normalize_hid_input(record))
    if reply.message_type != expected_type:
        raise ValueError(f"unexpected V30 reply type: {reply.message_type}")
    if reply.sequence != sequence:
        raise ValueError(f"V30 reply sequence mismatch: {reply.sequence} != {sequence}")
    if reply.payload != expected_payload:
        raise ValueError(f"unexpected V30 reply payload: {reply.payload!r}")
    return reply


def validate_live_reply(record: bytes, request: Message) -> Message:
    expected_type = TYPE_RESULT if request.message_type == TYPE_COMMAND else TYPE_HEARTBEAT
    expected_payload = COMMAND_REPLY if request.message_type == TYPE_COMMAND else HEARTBEAT_REPLY
    reply = validate_reply(record, request.sequence, expected_type, expected_payload)
    if reply.status != STATUS_OK:
        raise ValueError(f"V30 live reply status is not OK: {reply.status}")
    return reply


def _serial_module():
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required; run: py -m pip install -r "
            r"tools\ai_bridge\requirements.txt"
        ) from exc
    return serial


def _hid_module():
    try:
        import hid  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "hidapi is required; run: py -m pip install -r "
            r"tools\ai_bridge\requirements.txt"
        ) from exc
    return hid


def list_hid_devices(vid: int = USB_VID, pid: int = USB_PID) -> list[dict[str, Any]]:
    hid = _hid_module()
    devices: list[dict[str, Any]] = []
    for item in hid.enumerate(vid, pid):
        native_path = item.get("path")
        devices.append(
            {
                "vid": f"{item['vendor_id']:04X}",
                "pid": f"{item['product_id']:04X}",
                "product": item.get("product_string") or "",
                "serial": item.get("serial_number") or "",
                "interface": item.get("interface_number"),
                "usage_page": item.get("usage_page"),
                "usage": item.get("usage"),
                "path": native_path.decode(errors="replace")
                if isinstance(native_path, bytes)
                else str(native_path or ""),
                "_native_path": native_path,
            }
        )
    return devices


def _open_hid(serial_number: str | None = None):
    hid = _hid_module()
    matches = list_hid_devices()
    if serial_number is not None:
        matches = [item for item in matches if item["serial"] == serial_number]
    if len(matches) != 1:
        detail = ", ".join(
            f"serial={item['serial'] or '<none>'} interface={item['interface']}"
            for item in matches
        ) or "none"
        raise RuntimeError(
            "expected exactly one pi86-rp2350 HID interface "
            f"(VID {USB_VID:04X}, PID {USB_PID:04X}); found {len(matches)}: {detail}"
        )
    device = hid.device()
    device.open_path(matches[0]["_native_path"])
    device.set_nonblocking(1)
    identity = {key: value for key, value in matches[0].items() if key != "_native_path"}
    return device, identity


def default_output_dir() -> Path:
    configured = os.environ.get("PI86_VALIDATION_LOG_DIR")
    if configured:
        return Path(configured).expanduser()
    documents = Path.home() / "Documents"
    base = documents if documents.is_dir() else Path.home()
    return base / "pi86-validation-logs"


def physical_exchange(
    port: str,
    sequence: int,
    timeout: float,
    output_dir: Path,
    serial_number: str | None = None,
    echo_cdc: bool = True,
) -> tuple[dict[str, Any], int]:
    """Perform one HID round trip while retaining the CDC physical evidence."""
    serial = _serial_module()
    request = Message(TYPE_HELLO, sequence, CANONICAL_GREETING)
    request_record = request.encode()
    captured = bytearray()
    hid_reply_raw: bytes | None = None
    hid_device = None
    connection = None
    hid_identity: dict[str, Any] = {}
    transport_errors: list[str] = []
    started = datetime.now().astimezone()

    try:
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0, write_timeout=1.0
        )
        connection.dtr = True
        time.sleep(0.15)
        connection.reset_input_buffer()

        hid_device, hid_identity = _open_hid(serial_number)
        written = hid_device.write(hid_output_report(request_record))
        if written != MESSAGE_SIZE + 1:
            raise RuntimeError(f"short HID write: {written}/{MESSAGE_SIZE + 1} bytes")

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            waiting = connection.in_waiting
            if waiting:
                chunk = connection.read(waiting)
                captured.extend(chunk)
                if echo_cdc:
                    sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                    sys.stdout.flush()

            if hid_reply_raw is None:
                candidate = bytes(hid_device.read(MESSAGE_SIZE + 1))
                if candidate:
                    hid_reply_raw = normalize_hid_input(candidate)

            if any(marker in captured for marker in TERMINAL_MARKERS) and hid_reply_raw is not None:
                break
            time.sleep(0.005)
        else:
            transport_errors.append(f"exchange timed out after {timeout:.1f} seconds")
    except (OSError, RuntimeError, ValueError) as exc:
        transport_errors.append(str(exc))
    finally:
        if hid_device is not None:
            hid_device.close()
        if connection is not None:
            connection.close()

    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = started.strftime("%Y%m%d_%H%M%S%z")
    text = captured.decode("utf-8", errors="replace")
    profile = COMPANION_RUNTIME if "[PERSISTENT COMPANION RUNTIME]" in text else AI_B2_HID
    raw_path = output_dir / f"{profile.filename_prefix}_{timestamp}.log"
    raw_path.write_bytes(captured)
    cdc_report = validate_output(text, profile)
    story = list(explain_output(text, cdc_report))

    cdc_sequence = None
    sequence_match = re.search(
        r"(?m)^Windows HID 64-byte record\s+PASS \(sequence ([0-9]+)\)\s*$",
        text,
    ) if profile is AI_B2_HID else None
    if sequence_match is not None:
        cdc_sequence = int(sequence_match.group(1))
        if cdc_sequence != sequence:
            transport_errors.append(
                f"CDC/HID request sequence mismatch: {cdc_sequence} != {sequence}"
            )

    reply: Message | None = None
    if hid_reply_raw is None:
        transport_errors.append("no complete 64-byte HID reply was received")
    else:
        try:
            expected_type = TYPE_HEARTBEAT if profile is COMPANION_RUNTIME else TYPE_TEXT
            expected_payload = HEARTBEAT_REPLY if profile is COMPANION_RUNTIME else CANONICAL_REPLY
            reply = validate_reply(hid_reply_raw, sequence, expected_type, expected_payload)
        except ValueError as exc:
            transport_errors.append(str(exc))

    hid_pass = reply is not None
    cdc_pass = cdc_report.passed
    overall_pass = hid_pass and cdc_pass and not transport_errors
    result: dict[str, Any] = {
        "schema": "pi86-rp2350.ai-bridge.exchange/v1",
        "profile": profile.name,
        "timestamp": started.isoformat(),
        "request": {
            "transport": "USB HID",
            "bytes": MESSAGE_SIZE,
            "type": request.message_type,
            "sequence": request.sequence,
            "payload": request.payload.decode("ascii"),
            "sha256": hashlib.sha256(request_record).hexdigest(),
        },
        "reply": None if reply is None else {
            "transport": "USB HID",
            "bytes": MESSAGE_SIZE,
            "type": reply.message_type,
            "sequence": reply.sequence,
            "payload": reply.payload.decode("ascii"),
            "sha256": hashlib.sha256(hid_reply_raw or b"").hexdigest(),
        },
        "hid": {"passed": hid_pass, "identity": hid_identity},
        "cdc_validation": {
            "role": "receive-only physical evidence",
            "passed": cdc_pass,
            "request_sequence": cdc_sequence,
            "checks_passed": list(cdc_report.passed_checks),
            "errors": list(cdc_report.errors),
            "raw_log": str(raw_path.resolve()),
            "raw_sha256": hashlib.sha256(captured).hexdigest(),
            "explanation": story,
        },
        "bus_safety": {
            "passed": "bus safety" in cdc_report.passed_checks
            and "terminal electrical state" in cdc_report.passed_checks,
            "terminal_state": "RESET=HIGH, CLK=LOW, AD=high-Z"
            if "terminal electrical state" in cdc_report.passed_checks
            else "unproven",
        },
        "errors": transport_errors,
        "passed": overall_pass,
    }
    json_path = output_dir / f"{profile.filename_prefix}_{timestamp}.json"
    result["result_json"] = str(json_path.resolve())
    json_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    return result, PASS_EXIT if overall_pass else VALIDATION_EXIT


def print_human_result(result: dict[str, Any]) -> None:
    request = result["request"]
    reply = result["reply"]
    print("\n[PHYSICAL V30 EXCHANGE]")
    print(f"OpenAI Codex > {request['payload']}  (HID, {request['bytes']} bytes)")
    if reply is None:
        print("NEC V30      > <no valid HID reply>")
    else:
        print(f"NEC V30      > {reply['payload']}  (HID, {reply['bytes']} bytes)")

    print("\n[CDC LOG EXPLANATION]")
    for index, sentence in enumerate(result["cdc_validation"]["explanation"], 1):
        print(f"{index}. {sentence}")

    cdc_errors = result["cdc_validation"]["errors"]
    if cdc_errors:
        print("\n[CDC VALIDATION ERRORS]")
        for error in cdc_errors:
            print(f"- {error}")

    print("\n[ARTIFACTS]")
    print(f"Raw CDC evidence = {result['cdc_validation']['raw_log']}")
    print(f"Codex JSON result = {result['result_json']}")
    for error in result["errors"]:
        print(f"ERROR            = {error}")
    print(f"AI BRIDGE RESULT = {'PASS' if result['passed'] else 'FAIL'}")


def _status_text(
    sequence: int,
    stats: HeartbeatStats,
    connected: bool,
    processor: str = "nec-v30",
) -> str:
    state = "ALIVE" if connected else "LOST"
    latency = f"{stats.last_ms:.1f} ms" if stats.completed else "--"
    processor_name = PROCESSOR_NAMES[processor]
    return (
        f"| {'●' if connected else '○'} {processor_name} {state}  "
        f"seq={sequence:03d}  last={latency}  lost={stats.lost}"
    )


class ConsoleStatus:
    """Own two terminal rows: immutable status above an editable prompt."""

    def __init__(self, processor: str) -> None:
        self._rows = 0
        self._tty = sys.stdout.isatty()
        self._processor = processor
        self._prompt = "8086" if processor == "intel-8086" else "V30"

    def _erase(self) -> None:
        if self._rows == 0 or not self._tty:
            return
        sys.stdout.write("\r\x1b[2K")
        if self._rows == 2:
            # Move from the prompt row to the status row and clear it too.
            sys.stdout.write("\x1b[1A\r\x1b[2K")

    def render(
        self,
        sequence: int,
        stats: HeartbeatStats,
        connected: bool,
        command_buffer: str,
    ) -> None:
        if not self._tty:
            return
        self._erase()
        sys.stdout.write(
            f"{_status_text(sequence, stats, connected, self._processor)}\n"
            f"{self._prompt}> {command_buffer}"
        )
        sys.stdout.flush()
        self._rows = 2

    def render_prompt(self, command_buffer: str) -> None:
        if not self._tty:
            return
        self._erase()
        sys.stdout.write(f"{self._prompt}> {command_buffer}")
        sys.stdout.flush()
        self._rows = 1

    def clear(self) -> None:
        self._erase()
        if self._tty:
            sys.stdout.flush()
        self._rows = 0


def _read_windows_command(buffer: str) -> tuple[str, str | None, bool]:
    """Nonblocking single-line input for the Windows status display."""
    if os.name != "nt" or not sys.stdin.isatty():
        return buffer, None, False
    import msvcrt

    changed = False
    command: str | None = None
    while msvcrt.kbhit():
        char = msvcrt.getwch()
        if char in ("\r", "\n"):
            command = buffer.strip()
            buffer = ""
            changed = True
            break
        if char == "\x03":
            raise KeyboardInterrupt
        if char == "\b":
            buffer = buffer[:-1]
            changed = True
        elif char in ("\x00", "\xe0"):
            if msvcrt.kbhit():
                msvcrt.getwch()
        elif char.isprintable():
            buffer += char
            changed = True
    return buffer, command, changed


def persistent_monitor(
    port: str,
    sequence: int,
    timeout: float,
    interval: float,
    output_dir: Path,
    serial_number: str | None,
    display: str,
    interactive: bool,
    rounds: int,
    processor: str,
) -> int:
    """Keep one HID/CDC session synchronized with the living physical CPU."""
    serial = _serial_module()
    output_dir.mkdir(parents=True, exist_ok=True)
    started = datetime.now().astimezone()
    timestamp = started.strftime("%Y%m%d_%H%M%S%z")
    raw_path = output_dir / f"companion_heartbeat_{timestamp}.log"
    json_path = output_dir / f"companion_heartbeat_{timestamp}.json"
    captured = bytearray()
    events: list[dict[str, Any]] = []
    stats = HeartbeatStats()
    console = ConsoleStatus(processor)
    processor_name = PROCESSOR_NAMES[processor]
    command_buffer = ""
    connected = True
    current_sequence = sequence & 0xFFFFFFFF
    next_due = time.monotonic()
    stop = False
    transport_error: str | None = None

    connection = serial.Serial(
        port=port, baudrate=115200, timeout=0, write_timeout=1.0
    )
    connection.dtr = True
    hid_device, hid_identity = _open_hid(serial_number)

    def drain_cdc() -> None:
        nonlocal transport_error
        if transport_error is not None:
            return
        try:
            waiting = connection.in_waiting
            if waiting:
                chunk = connection.read(waiting)
                captured.extend(chunk)
        except (OSError, serial.SerialException) as exc:
            transport_error = f"USB CDC disconnected: {exc}"

    def exchange(request: Message) -> tuple[Message | None, float, str | None]:
        nonlocal transport_error
        began = time.monotonic()
        try:
            while bytes(hid_device.read(MESSAGE_SIZE + 1)):
                pass
            record = request.encode()
            written = hid_device.write(hid_output_report(record))
            if written != MESSAGE_SIZE + 1:
                return None, 0.0, f"short HID write: {written}/{MESSAGE_SIZE + 1} bytes"
            deadline = began + timeout
            while time.monotonic() <= deadline:
                drain_cdc()
                if transport_error is not None:
                    return None, (time.monotonic() - began) * 1000.0, transport_error
                candidate = bytes(hid_device.read(MESSAGE_SIZE + 1))
                if candidate:
                    try:
                        reply = validate_live_reply(candidate, request)
                    except ValueError as exc:
                        return None, (time.monotonic() - began) * 1000.0, str(exc)
                    # Latency ends at the complete, sequence-bound HID reply.
                    # The following CDC drain preserves evidence but is not part
                    # of the physical V30 request/reply round-trip measurement.
                    latency_ms = (time.monotonic() - began) * 1000.0
                    # Firmware publishes its concise CDC proof immediately after
                    # the HID reply. Retain it without delaying the next V30 IRQ.
                    drain_deadline = time.monotonic() + 0.05
                    while time.monotonic() < drain_deadline:
                        drain_cdc()
                        if transport_error is not None:
                            break
                        time.sleep(0.001)
                    return reply, latency_ms, None
                time.sleep(0.001)
        except OSError as exc:
            transport_error = f"USB HID disconnected: {exc}"
            return None, (time.monotonic() - began) * 1000.0, transport_error
        return None, (time.monotonic() - began) * 1000.0, "heartbeat timeout"

    def print_event(text: str) -> None:
        console.clear()
        print(text)
        if interactive:
            if display == "status":
                console.render(current_sequence, stats, connected, command_buffer)
            else:
                console.render_prompt(command_buffer)

    if interactive:
        print(f"\n[{processor_name} INTERACTIVE HEARTBEAT]")
        print("Host runtime shell: type help for the complete command framework.")
        print("Heartbeat runs in the background; command traffic has priority.\n")
        if display == "status":
            console.render(current_sequence, stats, connected, command_buffer)
        else:
            console.render_prompt(command_buffer)

    try:
        while not stop and (rounds == 0 or stats.completed + stats.lost < rounds):
            drain_cdc()
            if transport_error is not None:
                print_event(transport_error)
                stop = True
                continue
            command: str | None = None
            if interactive:
                command_buffer, command, changed = _read_windows_command(command_buffer)
                if changed and display == "status":
                    console.render(
                        current_sequence, stats, connected, command_buffer
                    )
                elif changed:
                    console.render_prompt(command_buffer)

            request_type: int | None = None
            request_payload = b""
            is_command = False
            if command is not None:
                try:
                    shell_command = parse_command(command)
                except ValueError as exc:
                    print_event(str(exc))
                    shell_command = None
                if shell_command is None:
                    continue
                name = shell_command.spec.name
                arguments = shell_command.arguments
                if name == "quit":
                    stop = True
                    continue
                if name == "help":
                    try:
                        print_event(command_help(arguments[0] if arguments else None))
                    except ValueError as exc:
                        print_event(str(exc))
                elif name in ("status", "top"):
                    print_event(
                        f"{processor_name} ALIVE={connected} completed={stats.completed} "
                        f"lost={stats.lost} min/avg/max="
                        f"{stats.minimum_ms if stats.completed else 0:.1f}/"
                        f"{stats.average_ms:.1f}/{stats.maximum_ms:.1f} ms"
                    )
                    if name == "top":
                        print_event(
                            "Physical processor runtime top\n"
                            f"  {processor_name:<10} {'ALIVE' if connected else 'NOT RESPONDING'} @ 1.000 MHz\n"
                            f"  Heartbeat {stats.completed} completed / {stats.lost} lost\n"
                            f"  Latency   {stats.average_ms:.1f} ms average\n"
                            "  Workload  NOT AVAILABLE\n"
                            "  PSRAM     NOT AVAILABLE\n"
                            "  flash:    NOT AVAILABLE\n"
                            "  sd:       NOT AVAILABLE"
                        )
                elif name == "info":
                    print_event(
                        "Negotiated capabilities:\n"
                        "  heartbeat  AVAILABLE\n"
                        "  console    bounded 14-byte command exchange\n"
                        "  workload   NOT AVAILABLE\n"
                        "  memory     NOT AVAILABLE\n"
                        "  filesystem NOT AVAILABLE\n"
                        "  storage    NOT AVAILABLE\n"
                        "  sd         NOT AVAILABLE\n"
                        "  trace      NOT AVAILABLE"
                    )
                elif name == "quiet":
                    display = "quiet"
                    print_event("Heartbeat display: quiet (errors and commands only)")
                elif name == "verbose":
                    display = "verbose"
                    print_event("Heartbeat display: verbose")
                elif name == "ping":
                    request_type = TYPE_HEARTBEAT
                    request_payload = heartbeat_payload(current_sequence)
                    is_command = True
                elif name == "console":
                    print_event(
                        "Console is active. Use: send <text>\n"
                        "Current native mailbox payload limit: 14 bytes"
                    )
                elif name == "send":
                    payload = " ".join(arguments).encode("utf-8")
                    if not payload:
                        print_event("Usage: send <text>")
                    if len(payload) > 14:
                        print_event("Command rejected: current native mailbox consumes at most 14 bytes")
                    elif payload:
                        request_type = TYPE_COMMAND
                        request_payload = payload
                        is_command = True
                else:
                    print_event(unavailable_message(shell_command))

            now = time.monotonic()
            if request_type is None and now >= next_due:
                request_type = TYPE_HEARTBEAT
                request_payload = heartbeat_payload(current_sequence)
            if request_type is None:
                time.sleep(0.02)
                continue

            request = Message(request_type, current_sequence, request_payload)
            reply, latency_ms, error = exchange(request)
            event = {
                "sequence": current_sequence,
                "request_type": request_type,
                "latency_ms": round(latency_ms, 3),
                "passed": reply is not None,
                "error": error,
            }
            events.append(event)
            if reply is not None:
                stats.accept(latency_ms)
                connected = True
                if display == "verbose" or is_command:
                    reply_text = reply.payload.decode("ascii")
                    if processor == "intel-8086" and reply_text.startswith("V30 "):
                        reply_text = "8086 " + reply_text[4:]
                    print_event(
                        f"[{current_sequence:03d}] {reply_text}  "
                        f"latency={latency_ms:.1f} ms"
                    )
            else:
                stats.lost += 1
                connected = False
                print_event(
                    f"[{current_sequence:03d}] {processor_name} HEARTBEAT LOST  "
                    f"latency={latency_ms:.1f} ms  error={error}"
                )
            if display == "status":
                console.render(
                    current_sequence, stats, connected, command_buffer
                )
            current_sequence = (current_sequence + 1) & 0xFFFFFFFF
            if current_sequence == 0:
                current_sequence = 1
            next_due = time.monotonic() + interval
    except KeyboardInterrupt:
        stop = True
    finally:
        console.clear()
        drain_cdc()
        try:
            hid_device.close()
        except OSError:
            pass
        try:
            connection.close()
        except (OSError, serial.SerialException):
            pass
        raw_path.write_bytes(captured)
        summary = {
            "schema": "pi86-rp2350.companion-heartbeat/v1",
            "started": started.isoformat(),
            "clock_hz": 1_000_000,
            "processor": processor,
            "processor_name": processor_name,
            "hid_identity": hid_identity,
            "completed": stats.completed,
            "lost": stats.lost,
            "latency_ms": {
                "last": stats.last_ms,
                "minimum": 0.0 if not stats.completed else stats.minimum_ms,
                "average": stats.average_ms,
                "maximum": stats.maximum_ms,
            },
            "events": events,
            "transport_error": transport_error,
            "raw_cdc_log": str(raw_path.resolve()),
            "passed": stats.completed > 0 and stats.lost == 0 and
            transport_error is None,
        }
        json_path.write_text(
            json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(
            f"{processor_name} heartbeat stopped: completed={stats.completed} lost={stats.lost} "
            f"avg={stats.average_ms:.1f} ms"
        )
        print(f"Raw CDC evidence = {raw_path}")
        print(f"Session JSON     = {json_path}")
    if transport_error is not None:
        return TRANSPORT_EXIT
    return PASS_EXIT if stats.completed > 0 and stats.lost == 0 else VALIDATION_EXIT


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="exchange fixed 64-byte records with a physical 8086-class processor"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--simulate", action="store_true")
    mode.add_argument("--exchange", action="store_true")
    mode.add_argument("--interactive", action="store_true")
    mode.add_argument("--list-devices", action="store_true")
    parser.add_argument("--port", help="composite CDC port, for example COM14")
    parser.add_argument("--sequence", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--hid-serial")
    parser.add_argument(
        "--processor",
        choices=tuple(PROCESSOR_NAMES),
        default="nec-v30",
        help="installed physical processor identity (default: nec-v30)",
    )
    parser.add_argument("--output-dir", type=Path, default=default_output_dir())
    parser.add_argument(
        "--heartbeat", action="store_true",
        help="continue with host-driven physical-processor heartbeat after acceptance",
    )
    parser.add_argument(
        "--attach", action="store_true",
        help="attach to an already-running companion runtime without RESET evidence",
    )
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--heartbeat-timeout", type=float, default=2.0)
    parser.add_argument("--rounds", type=int, default=0, help="0 means run until Ctrl+C")
    parser.add_argument(
        "--display", choices=("quiet", "status", "verbose"), default="status"
    )
    parser.add_argument("--json", action="store_true", help="print only stable JSON")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.simulate:
        request = Message(TYPE_HELLO, args.sequence, CANONICAL_GREETING)
        response = Message.decode(simulate_v30(request.encode()))
        if args.json:
            print(json.dumps({
                "schema": "pi86-rp2350.ai-bridge.simulation/v1",
                "request": request.payload.decode("ascii"),
                "reply": response.payload.decode("ascii"),
                "sequence": response.sequence,
                "passed": True,
            }, separators=(",", ":")))
        else:
            print(f"OpenAI Codex > {request.payload.decode('ascii')}")
            print(f"NEC V30      > {response.payload.decode('ascii')}")
            print("Protocol simulation: PASS")
        return PASS_EXIT

    if args.list_devices:
        try:
            devices = list_hid_devices()
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return DEPENDENCY_EXIT
        if args.json:
            public = [{k: v for k, v in item.items() if k != "_native_path"} for item in devices]
            print(json.dumps(public, indent=2, ensure_ascii=False))
        elif not devices:
            print(f"No VID {USB_VID:04X} PID {USB_PID:04X} HID interface found.")
        else:
            for item in devices:
                print(
                    f"VID {item['vid']} PID {item['pid']}  "
                    f"serial={item['serial'] or '<none>'}  {item['product']}"
                )
        return PASS_EXIT if devices else TRANSPORT_EXIT

    if not args.port:
        parser.error("physical exchange requires --port COMxx")
    if args.interval <= 0:
        parser.error("--interval must be greater than zero")
    if args.heartbeat_timeout <= 0:
        parser.error("--heartbeat-timeout must be greater than zero")
    if args.rounds < 0:
        parser.error("--rounds cannot be negative")
    if args.json and (args.interactive or args.heartbeat):
        parser.error("persistent heartbeat display cannot be combined with --json")
    if args.attach and not (args.interactive or args.heartbeat):
        parser.error("--attach requires --interactive or --heartbeat")
    if args.attach:
        try:
            return persistent_monitor(
                port=args.port,
                sequence=args.sequence or 1,
                timeout=args.heartbeat_timeout,
                interval=args.interval,
                output_dir=args.output_dir,
                serial_number=args.hid_serial,
                display=args.display,
                interactive=args.interactive,
                rounds=args.rounds,
                processor=args.processor,
            )
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return DEPENDENCY_EXIT
    try:
        result, exit_code = physical_exchange(
            port=args.port,
            sequence=args.sequence,
            timeout=args.timeout,
            output_dir=args.output_dir,
            serial_number=args.hid_serial,
            echo_cdc=not args.json and not args.interactive,
        )
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return DEPENDENCY_EXIT
    if args.json:
        print(json.dumps(result, separators=(",", ":"), ensure_ascii=False))
    else:
        print_human_result(result)
    if exit_code != PASS_EXIT:
        return exit_code
    if args.interactive or args.heartbeat:
        return persistent_monitor(
            port=args.port,
            sequence=(args.sequence + 1) & 0xFFFFFFFF or 1,
            timeout=args.heartbeat_timeout,
            interval=args.interval,
            output_dir=args.output_dir,
            serial_number=args.hid_serial,
            display=args.display,
            interactive=args.interactive,
            rounds=args.rounds,
            processor=args.processor,
        )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
