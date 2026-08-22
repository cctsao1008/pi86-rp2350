#!/usr/bin/env python3
"""Windows HID bridge and CDC evidence collector for pi86-rp2350."""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time
from typing import Any

from physical_validator import AI_B2_HID, explain_output, validate_output
from protocol import MESSAGE_SIZE, Message, TYPE_HELLO, TYPE_TEXT

CANONICAL_GREETING = b"HELLO NEC V30"
CANONICAL_REPLY = b"HELLO OPENAI CODEX"
USB_VID = 0xCAFE
USB_PID = 0x4011
TERMINAL_MARKER = AI_B2_HID.end_marker.encode("ascii")

PASS_EXIT = 0
DEPENDENCY_EXIT = 3
TRANSPORT_EXIT = 4
VALIDATION_EXIT = 5


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


def validate_reply(record: bytes, sequence: int) -> Message:
    reply = Message.decode(normalize_hid_input(record))
    if reply.message_type != TYPE_TEXT:
        raise ValueError(f"unexpected V30 reply type: {reply.message_type}")
    if reply.sequence != sequence:
        raise ValueError(f"V30 reply sequence mismatch: {reply.sequence} != {sequence}")
    if reply.payload != CANONICAL_REPLY:
        raise ValueError(f"unexpected V30 reply payload: {reply.payload!r}")
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

            if TERMINAL_MARKER in captured and hid_reply_raw is not None:
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
    raw_path = output_dir / f"ai_b2_hid_{timestamp}.log"
    raw_path.write_bytes(captured)

    text = captured.decode("utf-8", errors="replace")
    cdc_report = validate_output(text, AI_B2_HID)
    story = list(explain_output(text, cdc_report))

    cdc_sequence = None
    sequence_match = re.search(
        r"(?m)^Windows HID 64-byte record\s+PASS \(sequence ([0-9]+)\)\s*$",
        text,
    )
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
            reply = validate_reply(hid_reply_raw, sequence)
        except ValueError as exc:
            transport_errors.append(str(exc))

    hid_pass = reply is not None
    cdc_pass = cdc_report.passed
    overall_pass = hid_pass and cdc_pass and not transport_errors
    result: dict[str, Any] = {
        "schema": "pi86-rp2350.ai-bridge.exchange/v1",
        "profile": AI_B2_HID.name,
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
    json_path = output_dir / f"ai_b2_hid_{timestamp}.json"
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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="exchange one fixed 64-byte message with a physical NEC V30"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--simulate", action="store_true")
    mode.add_argument("--exchange", action="store_true")
    mode.add_argument("--list-devices", action="store_true")
    parser.add_argument("--port", help="composite CDC port, for example COM14")
    parser.add_argument("--sequence", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--hid-serial")
    parser.add_argument("--output-dir", type=Path, default=default_output_dir())
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
        parser.error("--exchange requires --port COMxx")
    try:
        result, exit_code = physical_exchange(
            port=args.port,
            sequence=args.sequence,
            timeout=args.timeout,
            output_dir=args.output_dir,
            serial_number=args.hid_serial,
            echo_cdc=not args.json,
        )
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return DEPENDENCY_EXIT
    if args.json:
        print(json.dumps(result, separators=(",", ":"), ensure_ascii=False))
    else:
        print_human_result(result)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
