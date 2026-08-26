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
import queue
import re
import secrets
import struct
import sys
import time
from typing import Any

RUNTIME_TOOLS = Path(__file__).resolve().parents[1] / "runtime"
if str(RUNTIME_TOOLS) not in sys.path:
    sys.path.insert(0, str(RUNTIME_TOOLS))

from host_shell import command_help, parse_command, unavailable_message
from host_broker import (
    BrokerClient,
    BrokerRecord,
    DeviceBroker,
    discover_brokers,
    select_broker,
)
from workload import control_record, workload_from_command

from physical_validator import (
    AI_B2_HID,
    COMPANION_RUNTIME,
    explain_output,
    validate_output,
)
from protocol import (
    MESSAGE_SIZE,
    Message,
    NativeServiceWitness,
    STATUS_OK,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    TYPE_HELLO,
    TYPE_RESULT,
    TYPE_TEXT,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_COMMIT,
    TYPE_WORKLOAD_CONTROL,
    TYPE_WORKLOAD_DATA,
    TYPE_WORKLOAD_RESULT,
    TYPE_WORKLOAD_STATUS,
)

CANONICAL_GREETING = b"HELLO NEC V30"
CANONICAL_REPLY = b"HELLO OPENAI CODEX"
BOOTLOADER_REQUEST = b"PI86 BOOTLOADER\n"
BOOTLOADER_ACK = b"PI86 BOOTLOADER ACK"
STATUS_REQUEST = b"PI86 STATUS\n"
STATUS_BEGIN = b"PI86 STATUS BEGIN"
STATUS_END = b"PI86 STATUS END"
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


def validate_live_reply(
    record: bytes,
    request: Message,
    expected_processor: str | None = None,
) -> Message:
    expected_type = TYPE_RESULT if request.message_type == TYPE_COMMAND else TYPE_HEARTBEAT
    expected_payload = COMMAND_REPLY if request.message_type == TYPE_COMMAND else HEARTBEAT_REPLY
    reply = Message.decode(normalize_hid_input(record))
    if reply.message_type != expected_type:
        raise ValueError(f"unexpected native reply type: {reply.message_type}")
    if reply.sequence != request.sequence:
        raise ValueError(
            f"native reply sequence mismatch: {reply.sequence} != {request.sequence}"
        )
    if reply.status != STATUS_OK:
        raise ValueError(f"V30 live reply status is not OK: {reply.status}")
    witness = NativeServiceWitness.decode(reply.payload)
    if witness.service_type != request.message_type:
        raise ValueError(
            "native witness service type mismatch: "
            f"{witness.service_type} != {request.message_type}"
        )
    if witness.text != expected_payload:
        raise ValueError(f"unexpected native reply text: {witness.text!r}")
    if expected_processor is not None and witness.processor != expected_processor:
        raise ValueError(
            "native processor identity mismatch: "
            f"AAD16={witness.processor or 'unknown'} host={expected_processor}"
        )
    return reply


def validate_device_reply(
    record: bytes,
    request: Message,
    expected_processor: str | None = None,
) -> Message:
    """Validate either the deployed heartbeat ABI or the workload ABI."""
    if request.message_type in (TYPE_COMMAND, TYPE_HEARTBEAT):
        return validate_live_reply(record, request, expected_processor)

    if request.message_type not in (
        TYPE_WORKLOAD_BEGIN,
        TYPE_WORKLOAD_DATA,
        TYPE_WORKLOAD_COMMIT,
        TYPE_WORKLOAD_CONTROL,
    ):
        raise ValueError(f"unsupported request type: {request.message_type}")
    reply = Message.decode(normalize_hid_input(record))
    allowed_types = (TYPE_WORKLOAD_RESULT, TYPE_WORKLOAD_STATUS)
    if reply.message_type not in allowed_types:
        raise ValueError(f"unexpected workload reply type: {reply.message_type}")
    if reply.sequence != request.sequence:
        raise ValueError(
            f"workload reply sequence mismatch: {reply.sequence} != {request.sequence}"
        )
    if reply.status != STATUS_OK:
        raise ValueError(f"workload request status is not OK: {reply.status}")
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


def send_bootloader_request(connection: Any, timeout: float) -> bytes:
    """Request UF2 mode over CDC and require an acknowledgement first."""
    connection.write(BOOTLOADER_REQUEST)
    connection.flush()
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() <= deadline:
        try:
            chunk = connection.read(4096)
            if chunk:
                received.extend(chunk)
                if BOOTLOADER_ACK in received:
                    return bytes(received)
        except OSError as exc:
            # A disconnect is expected only after the acknowledgement.  The
            # retained evidence distinguishes that from an early disconnect.
            if BOOTLOADER_ACK in received:
                return bytes(received)
            raise RuntimeError(f"CDC disconnected before bootloader ACK: {exc}") from exc
        time.sleep(0.005)
    raise RuntimeError("timed out waiting for RP2350 bootloader ACK")


def send_status_request(connection: Any, timeout: float) -> bytes:
    """Request one freshly framed runtime status block over CDC."""
    connection.write(STATUS_REQUEST)
    connection.flush()
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() <= deadline:
        try:
            chunk = connection.read(4096)
        except OSError as exc:
            raise RuntimeError(
                f"CDC disconnected before status completed: {exc}"
            ) from exc
        if chunk:
            received.extend(chunk)
            end = received.find(STATUS_END)
            if end >= 0:
                begin = received.rfind(STATUS_BEGIN, 0, end)
                if begin < 0:
                    raise RuntimeError(
                        "status end marker arrived without begin marker"
                    )
                return bytes(received[begin:end + len(STATUS_END)])
        time.sleep(0.005)
    raise RuntimeError("timed out waiting for complete RP2350 status")


def request_status(port: str, timeout: float) -> int:
    """Print canonical runtime status without requiring HID."""
    serial = _serial_module()
    try:
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0.05, write_timeout=1.0
        )
        connection.dtr = True
        time.sleep(0.1)
        evidence = send_status_request(connection, timeout)
    except (OSError, serial.SerialException, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return TRANSPORT_EXIT
    finally:
        if "connection" in locals():
            try:
                connection.close()
            except (OSError, serial.SerialException):
                pass

    print_status_evidence(evidence)
    return PASS_EXIT


def print_status_evidence(evidence: bytes) -> None:
    """Print one framed canonical CDC status block."""
    text = evidence.decode("utf-8", errors="replace")
    lines = text.replace("\r\n", "\n").splitlines()
    if lines and lines[0] == STATUS_BEGIN.decode("ascii"):
        lines = lines[1:]
    if lines and lines[-1] == STATUS_END.decode("ascii"):
        lines = lines[:-1]
    print("\n".join(lines))
    print("RP2350 STATUS RESULT      = PASS")


def request_bootloader(port: str, timeout: float) -> int:
    """Enter RP2350 UF2 mode without requiring the HID runtime transport."""
    serial = _serial_module()
    try:
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0.05, write_timeout=1.0
        )
        connection.dtr = True
        time.sleep(0.1)
        evidence = send_bootloader_request(connection, timeout)
    except (OSError, serial.SerialException, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return TRANSPORT_EXIT
    finally:
        if "connection" in locals():
            try:
                connection.close()
            except (OSError, serial.SerialException):
                pass
    print("RP2350 bootloader request = ACKNOWLEDGED")
    print("RP2350 UF2 bootloader     = ENTERING")
    if evidence:
        print(f"CDC evidence bytes        = {len(evidence)}")
    return PASS_EXIT


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


def list_cdc_ports() -> list[dict[str, str]]:
    """Return CDC interfaces belonging to the pi86-rp2350 composite device."""
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required for automatic CDC port discovery"
        ) from exc

    matches: list[dict[str, str]] = []
    for item in list_ports.comports():
        if item.vid == USB_VID and item.pid == USB_PID:
            matches.append(
                {
                    "port": item.device,
                    "serial": item.serial_number or "",
                    "description": item.description or "",
                }
            )
    return matches


def select_cdc_port(
    candidates: list[dict[str, str]], serial_number: str | None = None
) -> str:
    """Select one unambiguous CDC interface, optionally by USB serial."""
    matches = candidates
    if serial_number is not None:
        matches = [item for item in matches if item["serial"] == serial_number]
    if len(matches) == 1:
        return matches[0]["port"]
    detail = ", ".join(
        f"{item['port']} (serial={item['serial'] or '<none>'})" for item in matches
    ) or "none"
    qualifier = f" with serial {serial_number}" if serial_number else ""
    raise RuntimeError(
        "expected exactly one pi86-rp2350 CDC interface "
        f"(VID {USB_VID:04X}, PID {USB_PID:04X}){qualifier}; "
        f"found {len(matches)}: {detail}. Use --port COMxx to select manually."
    )


def resolve_cdc_port(
    explicit_port: str | None, serial_number: str | None = None
) -> tuple[str, bool]:
    """Resolve a manual port or discover the unique composite CDC port."""
    if explicit_port:
        return explicit_port, False
    return select_cdc_port(list_cdc_ports(), serial_number), True


def cdc_serial_for_port(
    port: str, candidates: list[dict[str, str]] | None = None
) -> str | None:
    """Return the USB serial paired with a CDC port when it is enumerable."""
    if candidates is None:
        candidates = list_cdc_ports()
    matches = [
        item for item in candidates if item["port"].casefold() == port.casefold()
    ]
    if len(matches) == 1 and matches[0]["serial"]:
        return matches[0]["serial"]
    return None


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
    processor: str = "nec-v30",
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
            if profile is COMPANION_RUNTIME:
                # The persistent runtime now returns the same processor-owned
                # witness used by every later heartbeat.  Do not compare the
                # complete payload with the legacy bare text string.
                reply = validate_live_reply(hid_reply_raw, request, processor)
            else:
                reply = validate_reply(
                    hid_reply_raw, sequence, TYPE_TEXT, CANONICAL_REPLY
                )
        except ValueError as exc:
            transport_errors.append(str(exc))

    hid_pass = reply is not None
    cdc_pass = cdc_report.passed
    overall_pass = hid_pass and cdc_pass and not transport_errors
    reply_json: dict[str, Any] | None = None
    if reply is not None:
        reply_text = reply.payload
        native_witness: NativeServiceWitness | None = None
        if profile is COMPANION_RUNTIME:
            native_witness = NativeServiceWitness.decode(reply.payload)
            reply_text = native_witness.text
        reply_json = {
            "transport": "USB HID",
            "bytes": MESSAGE_SIZE,
            "type": reply.message_type,
            "sequence": reply.sequence,
            "payload": reply_text.decode("ascii"),
            "sha256": hashlib.sha256(hid_reply_raw or b"").hexdigest(),
        }
        if native_witness is not None:
            reply_json["native_witness"] = {
                "boot_id": native_witness.boot_id,
                "cpu_sequence": native_witness.cpu_sequence,
                "command_sequence": native_witness.command_sequence,
                "service_type": native_witness.service_type,
                "processor": native_witness.processor,
                "identity_source": "physical AAD 16 discriminator",
            }
    result: dict[str, Any] = {
        "schema": "pi86-rp2350.ai-bridge.exchange/v1",
        "profile": profile.name,
        "processor": processor,
        "processor_name": PROCESSOR_NAMES[processor],
        "timestamp": started.isoformat(),
        "request": {
            "transport": "USB HID",
            "bytes": MESSAGE_SIZE,
            "type": request.message_type,
            "sequence": request.sequence,
            "payload": request.payload.decode("ascii"),
            "sha256": hashlib.sha256(request_record).hexdigest(),
        },
        "reply": reply_json,
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
    processor_name = result.get("processor_name", "NEC V30")
    print(f"\n[PHYSICAL {processor_name} EXCHANGE]")
    print(f"OpenAI Codex > {request['payload']}  (HID, {request['bytes']} bytes)")
    if reply is None:
        print(f"{processor_name:<13}> <no valid HID reply>")
    else:
        print(
            f"{processor_name:<13}> {reply['payload']}  "
            f"(HID, {reply['bytes']} bytes)"
        )

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
    cpu_sequence: int | None,
    stats: HeartbeatStats,
    connected: bool,
    processor: str = "nec-v30",
) -> str:
    state = "ALIVE" if connected else "LOST"
    latency = f"{stats.last_ms:.1f} ms" if stats.completed else "--"
    sequence = "------" if cpu_sequence is None else f"{cpu_sequence:06d}"
    processor_name = PROCESSOR_NAMES[processor]
    return (
        f"| {'●' if connected else '○'} {processor_name} {state}  "
        f"cpu_seq={sequence}  rtt={latency}  lost={stats.lost}"
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
    broker_record: BrokerRecord | None = None,
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
    current_boot_id: int | None = None
    current_cpu_sequence: int | None = None
    current_command_sequence: int | None = None
    current_native_processor: str | None = None
    next_due = time.monotonic()
    stop = False
    transport_error: str | None = None

    connection = None
    hid_device = None
    owner_broker: DeviceBroker | None = None
    broker_client: BrokerClient | None = None
    if broker_record is not None:
        broker_client = BrokerClient(
            broker_record, f"pid-{os.getpid()}-{secrets.token_hex(4)}"
        )
        hello = broker_client.hello()
        if not hello.get("ok"):
            raise RuntimeError(f"broker handshake failed: {hello.get('error')}")
        hid_identity = {
            "serial": broker_record.device_id,
            "transport": "localhost-broker",
            "tcp_port": broker_record.tcp_port,
            "udp_port": broker_record.udp_port,
        }
    else:
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0, write_timeout=1.0
        )
        connection.dtr = True
        hid_device, hid_identity = _open_hid(serial_number)
        device_id = str(hid_identity.get("serial") or port)
        if select_broker(discover_brokers(), device_id) is not None:
            raise RuntimeError(f"device {device_id} already has an active broker")
        owner_broker = DeviceBroker(device_id, processor)
        owner_broker.start()

    def drain_cdc() -> None:
        nonlocal transport_error
        if transport_error is not None or connection is None:
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
        if broker_client is not None:
            try:
                response = broker_client.exchange(
                    request.encode(),
                    f"{os.getpid()}-{request.sequence}",
                    timeout,
                )
                latency_ms = float(response.get("latency_ms", 0.0))
                if not response.get("ok"):
                    return None, latency_ms, str(response.get("error") or "broker exchange failed")
                candidate = bytes.fromhex(str(response["reply_hex"]))
                return (
                    validate_device_reply(candidate, request, processor),
                    latency_ms,
                    None,
                )
            except (OSError, RuntimeError, ValueError, KeyError) as exc:
                transport_error = f"Host broker disconnected: {exc}"
                return None, (time.monotonic() - began) * 1000.0, transport_error
        try:
            assert hid_device is not None
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
                        reply = validate_device_reply(
                            candidate, request, processor
                        )
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

    def broker_snapshot() -> dict[str, Any]:
        return {
            "state": "OWNER_ACTIVE" if connected else "FAULT",
            "processor": processor,
            "request_sequence": current_sequence,
            "boot_id": current_boot_id,
            "cpu_sequence": current_cpu_sequence,
            "command_sequence": current_command_sequence,
            "native_processor": current_native_processor,
            "completed": stats.completed,
            "lost": stats.lost,
            "last_ms": stats.last_ms,
        }

    def service_broker_requests() -> None:
        nonlocal stop
        if owner_broker is None:
            return
        for pending in owner_broker.pending():
            try:
                request = Message.decode(pending.record)
                reply, latency_ms, error = exchange(request)
                if reply is None:
                    result = {"ok": False, "error": error, "latency_ms": latency_ms}
                else:
                    result = {
                        "ok": True,
                        "reply_hex": reply.encode().hex(),
                        "latency_ms": latency_ms,
                    }
            except (ValueError, RuntimeError) as exc:
                result = {"ok": False, "error": str(exc), "latency_ms": 0.0}
            pending.future.set_result(result)
        while True:
            try:
                pending_control = owner_broker.controls.get_nowait()
            except queue.Empty:
                break
            try:
                bootloader_requested = False
                if connection is None:
                    raise RuntimeError("broker does not own a CDC connection")
                if pending_control.command == "status":
                    evidence = send_status_request(
                        connection, pending_control.timeout
                    )
                elif pending_control.command == "bootloader":
                    evidence = send_bootloader_request(
                        connection, pending_control.timeout
                    )
                    bootloader_requested = True
                else:
                    raise RuntimeError(
                        f"unsupported CDC control: {pending_control.command}"
                    )
                captured.extend(evidence)
                control_result = {
                    "ok": True,
                    "evidence_hex": evidence.hex(),
                }
            except (OSError, RuntimeError, serial.SerialException) as exc:
                bootloader_requested = False
                control_result = {"ok": False, "error": str(exc)}
            pending_control.future.set_result(control_result)
            if bootloader_requested:
                # Let the TCP handler flush the ACK before shutting down the
                # broker event loop after the expected USB disconnect.
                time.sleep(0.05)
                stop = True
        owner_broker.publish(broker_snapshot())

    def print_event(text: str) -> None:
        console.clear()
        print(text)
        if interactive:
            console.render(
                current_cpu_sequence, stats, connected, command_buffer
            )

    def perform_workload_transaction(
        records: list[Message], description: str
    ) -> bool:
        nonlocal current_sequence, next_due
        for index, request in enumerate(records, 1):
            reply, latency_ms, error = exchange(request)
            if reply is None:
                print_event(
                    f"{description}: FAILED at record {index}/{len(records)}: {error}"
                )
                return False
            current_sequence = (request.sequence + 1) & 0xFFFFFFFF
            if current_sequence == 0:
                current_sequence = 1
            if display == "verbose":
                print_event(
                    f"{description}: record {index}/{len(records)} accepted "
                    f"({latency_ms:.1f} ms)"
                )
        next_due = time.monotonic() + interval
        print_event(f"{description}: PASS ({len(records)} records)")
        return True

    if interactive:
        print(f"\n[{processor_name} INTERACTIVE HEARTBEAT]")
        print("Host runtime shell: type help for the complete command framework.")
        print("Heartbeat runs in the background; command traffic has priority.\n")
        console.render(current_cpu_sequence, stats, connected, command_buffer)

    try:
        while not stop and (rounds == 0 or stats.completed + stats.lost < rounds):
            service_broker_requests()
            drain_cdc()
            if transport_error is not None:
                print_event(transport_error)
                stop = True
                continue
            command: str | None = None
            if interactive:
                command_buffer, command, changed = _read_windows_command(command_buffer)
                if changed:
                    console.render(
                        current_cpu_sequence, stats, connected, command_buffer
                    )

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
                        f"{stats.average_ms:.1f}/{stats.maximum_ms:.1f} ms\n"
                        f"boot_id={current_boot_id if current_boot_id is not None else '--'} "
                        f"cpu_seq={current_cpu_sequence if current_cpu_sequence is not None else '--'} "
                        f"command_seq={current_command_sequence if current_command_sequence is not None else '--'}"
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
                elif name == "load":
                    try:
                        transfer_id = secrets.randbits(32)
                        manifest, image, records = workload_from_command(
                            arguments,
                            transfer_id=transfer_id,
                            first_sequence=current_sequence,
                        )
                    except ValueError as exc:
                        print_event(f"load: {exc}")
                        continue
                    print_event(
                        "Native workload upload\n"
                        f"  image   {len(image)} bytes\n"
                        f"  address 0x{manifest.load_address:05X}\n"
                        f"  entry   {manifest.entry_segment:04X}:{manifest.entry_offset:04X}\n"
                        f"  CRC32   {manifest.image_crc32:08X}"
                    )
                    perform_workload_transaction(records, "workload upload")
                    continue
                elif name in ("run", "stop", "restart"):
                    record = control_record(
                        name, workload_id=0, sequence=current_sequence
                    )
                    perform_workload_transaction([record], f"workload {name}")
                    continue
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
                witness = NativeServiceWitness.decode(reply.payload)
                first_identity = current_native_processor is None
                if (
                    current_boot_id == witness.boot_id
                    and current_cpu_sequence is not None
                ):
                    delta = (
                        witness.cpu_sequence - current_cpu_sequence
                    ) & 0xFFFFFFFF
                    if delta == 0 or delta >= 0x80000000:
                        error = (
                            "stale native completion counter: "
                            f"{witness.cpu_sequence} after {current_cpu_sequence}"
                        )
                        reply = None
                if reply is not None:
                    current_boot_id = witness.boot_id
                    current_cpu_sequence = witness.cpu_sequence
                    current_command_sequence = witness.command_sequence
                    current_native_processor = witness.processor
                    event.update(
                        {
                            "boot_id": witness.boot_id,
                            "cpu_sequence": witness.cpu_sequence,
                            "command_sequence": witness.command_sequence,
                            "native_processor": witness.processor,
                        }
                    )
                    if first_identity:
                        print_event(
                            "[PROCESSOR IDENTITY] "
                            f"{processor_name} (native AAD 16) matches Host declaration"
                        )
            if reply is not None:
                stats.accept(latency_ms)
                connected = True
                if display == "verbose" or is_command:
                    if reply.message_type in (
                        TYPE_WORKLOAD_RESULT, TYPE_WORKLOAD_STATUS
                    ):
                        reply_text = "WORKLOAD REQUEST OK"
                    else:
                        reply_text = NativeServiceWitness.decode(
                            reply.payload
                        ).text.decode("ascii")
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
            if interactive:
                console.render(
                    current_cpu_sequence, stats, connected, command_buffer
                )
            current_sequence = (current_sequence + 1) & 0xFFFFFFFF
            if current_sequence == 0:
                current_sequence = 1
            next_due = time.monotonic() + interval
            if owner_broker is not None:
                owner_broker.publish(broker_snapshot())
    except KeyboardInterrupt:
        stop = True
    finally:
        console.clear()
        drain_cdc()
        if owner_broker is not None:
            owner_broker.stop()
        if hid_device is not None:
            try:
                hid_device.close()
            except OSError:
                pass
        if connection is not None:
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
            "boot_id": current_boot_id,
            "cpu_sequence": current_cpu_sequence,
            "command_sequence": current_command_sequence,
            "native_processor": current_native_processor,
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
    mode.add_argument(
        "--bootloader", action="store_true",
        help="request RP2350 UF2 bootloader mode over USB CDC",
    )
    mode.add_argument(
        "--status", action="store_true",
        help="request canonical RP2350 runtime status over USB CDC",
    )
    mode.add_argument("--list-devices", action="store_true")
    parser.add_argument(
        "--port",
        help="composite CDC port, for example COM14 (default: auto-detect)",
    )
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

    broker_record: BrokerRecord | None = None
    if args.status or args.bootloader or (
        args.attach and (args.interactive or args.heartbeat)
    ):
        device_hint = args.hid_serial
        if device_hint is None and args.port:
            try:
                device_hint = cdc_serial_for_port(args.port)
            except RuntimeError:
                device_hint = None
        try:
            broker_record = select_broker(discover_brokers(), device_hint)
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT

    if args.status and broker_record is not None:
        try:
            broker_client = BrokerClient(
                broker_record, f"status-{os.getpid()}"
            )
            reply = broker_client.hello()
            control = broker_client.control(
                "status", f"status-{os.getpid()}", args.timeout
            )
        except (OSError, RuntimeError, ValueError) as exc:
            print(f"ERROR: Host broker status failed: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT
        if not control.get("ok"):
            print(
                f"ERROR: Host broker status failed: {control.get('error')}",
                file=sys.stderr,
            )
            return TRANSPORT_EXIT
        snapshot = reply.get("snapshot", {})
        print("\n[HOST BROKER STATUS]")
        print(f"Device ID                  = {broker_record.device_id}")
        print(f"Physical processor         = {reply.get('processor', 'UNKNOWN').upper()}")
        print(f"Broker state               = {snapshot.get('state', 'UNKNOWN')}")
        print(f"TCP / UDP                  = {broker_record.tcp_port} / {broker_record.udp_port}")
        print(f"Heartbeat completed / lost = {snapshot.get('completed', 0)} / {snapshot.get('lost', 0)}")
        print("Hardware owner              = EXISTING BROKER")
        print("HOST BROKER STATUS RESULT = PASS")
        print_status_evidence(bytes.fromhex(str(control["evidence_hex"])))
        return PASS_EXIT

    if args.bootloader and broker_record is not None:
        try:
            control = BrokerClient(
                broker_record, f"bootloader-{os.getpid()}"
            ).control("bootloader", f"bootloader-{os.getpid()}", args.timeout)
        except (OSError, RuntimeError, ValueError) as exc:
            print(f"ERROR: Host broker bootloader failed: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT
        if not control.get("ok"):
            print(
                f"ERROR: Host broker bootloader failed: {control.get('error')}",
                file=sys.stderr,
            )
            return TRANSPORT_EXIT
        evidence = bytes.fromhex(str(control["evidence_hex"]))
        print("RP2350 bootloader request = ACKNOWLEDGED VIA HOST BROKER")
        print("RP2350 UF2 bootloader     = ENTERING")
        print(f"CDC evidence bytes        = {len(evidence)}")
        return PASS_EXIT

    if broker_record is not None:
        if not args.json:
            print(
                "Connected to existing Host broker = "
                f"{broker_record.device_id} (TCP {broker_record.tcp_port}, "
                f"{broker_record.processor})"
            )
        return persistent_monitor(
            port="",
            sequence=args.sequence or 1,
            timeout=args.heartbeat_timeout,
            interval=args.interval,
            output_dir=args.output_dir,
            serial_number=broker_record.device_id,
            display=args.display,
            interactive=args.interactive,
            rounds=args.rounds,
            processor=broker_record.processor,
            broker_record=broker_record,
        )

    try:
        args.port, auto_port = resolve_cdc_port(args.port, args.hid_serial)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return TRANSPORT_EXIT
    if auto_port and not args.json:
        print(f"Auto-selected CDC port = {args.port}")
    if args.hid_serial is None:
        try:
            args.hid_serial = cdc_serial_for_port(args.port)
        except RuntimeError:
            args.hid_serial = None
    if args.status:
        return request_status(args.port, args.timeout)
    if args.bootloader:
        return request_bootloader(args.port, args.timeout)
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
            processor=args.processor,
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
