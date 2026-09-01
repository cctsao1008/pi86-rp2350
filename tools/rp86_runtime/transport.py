"""CDC, HID, device discovery, and runtime-control transport."""

import os
from pathlib import Path
import sys
import time
from typing import Any

from .constants import (
    BOOTLOADER_ACK,
    BOOTLOADER_REQUEST,
    PASS_EXIT,
    REBOOT_ACK,
    REBOOT_REQUEST,
    STATUS_BEGIN,
    STATUS_END,
    STATUS_REQUEST,
    TRANSPORT_EXIT,
    USB_PID,
    USB_VID,
)
from .core import hid_output_report, normalize_hid_input
from .protocol import (
    MESSAGE_SIZE,
    Message,
    STATUS_OK,
    TYPE_RUNTIME_CONTROL,
    TYPE_RUNTIME_STATUS,
)


def _serial_module():
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required; run: py -m pip install -r "
            r"tools/rp86_runtime/requirements.txt"
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


def send_reboot_request(connection: Any, timeout: float) -> bytes:
    """Request a normal firmware reboot over CDC and require its ACK."""
    connection.write(REBOOT_REQUEST)
    connection.flush()
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() <= deadline:
        try:
            chunk = connection.read(4096)
            if chunk:
                received.extend(chunk)
                if REBOOT_ACK in received:
                    return bytes(received)
        except OSError as exc:
            if REBOOT_ACK in received:
                return bytes(received)
            raise RuntimeError(f"CDC disconnected before reboot ACK: {exc}") from exc
        time.sleep(0.005)
    raise RuntimeError("timed out waiting for RP2350 reboot ACK")


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


def request_reboot_cdc(port: str, timeout: float) -> int:
    """Reboot the canonical runtime over the CDC fallback path."""
    serial = _serial_module()
    try:
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0.05, write_timeout=1.0
        )
        connection.dtr = True
        time.sleep(0.1)
        evidence = send_reboot_request(connection, timeout)
    except (OSError, serial.SerialException, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return TRANSPORT_EXIT
    finally:
        if "connection" in locals():
            try:
                connection.close()
            except (OSError, serial.SerialException):
                pass
    print("RP2350 reboot request     = ACKNOWLEDGED VIA CDC FALLBACK")
    print("RP2350 canonical runtime  = RESTARTING")
    if evidence:
        print(f"CDC evidence bytes        = {len(evidence)}")
    return PASS_EXIT


def _hid_module():
    try:
        import hid  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "hidapi is required; run: py -m pip install -r "
            r"tools/rp86_runtime/requirements.txt"
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


def send_hid_runtime_control(
    operation: int, sequence: int, timeout: float,
    serial_number: str | None = None,
) -> dict[str, Any]:
    """Send one sequence-bound runtime control record and require its HID ACK."""
    device, identity = _open_hid(serial_number)
    try:
        exchange_hid_runtime_control(device, operation, sequence, timeout)
        return identity
    finally:
        try:
            device.close()
        except OSError:
            pass


def exchange_hid_runtime_control(
    device: Any, operation: int, sequence: int, timeout: float,
) -> Message:
    """Exchange runtime control using an already-owned HID device."""
    request = Message(TYPE_RUNTIME_CONTROL, sequence, bytes((operation,)))
    while bytes(device.read(MESSAGE_SIZE + 1)):
        pass
    written = device.write(hid_output_report(request.encode()))
    if written != MESSAGE_SIZE + 1:
        raise RuntimeError(
            f"short HID control write: {written}/{MESSAGE_SIZE + 1} bytes"
        )
    deadline = time.monotonic() + timeout
    while time.monotonic() <= deadline:
        candidate = bytes(device.read(MESSAGE_SIZE + 1))
        if not candidate:
            time.sleep(0.001)
            continue
        reply = Message.decode(normalize_hid_input(candidate))
        if reply.message_type != TYPE_RUNTIME_STATUS:
            raise RuntimeError(
                f"unexpected runtime control reply type: {reply.message_type}"
            )
        if reply.sequence != request.sequence:
            raise RuntimeError(
                f"runtime control sequence mismatch: {reply.sequence} != {request.sequence}"
            )
        if reply.status != STATUS_OK:
            raise RuntimeError(f"runtime control status is not OK: {reply.status}")
        if reply.payload != bytes((operation,)):
            raise RuntimeError("runtime control operation ACK mismatch")
        return reply
    raise RuntimeError("timed out waiting for HID runtime control ACK")


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
    configured = os.environ.get("RP86_VALIDATION_LOG_DIR")
    if configured:
        return Path(configured).expanduser()
    documents = Path.home() / "Documents"
    base = documents if documents.is_dir() else Path.home()
    return base / "pi86-validation-logs"
