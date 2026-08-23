#!/usr/bin/env python3
"""pi86 host console for the RP2350 USB CDC transport.

Bridges host stdin/stdout to the RP2350 USB CDC serial interface. The tool is
intended for pi86 virtual-BIOS diagnostics and ELKS headless console I/O.

Exit an interactive session with Ctrl-].
"""

from __future__ import annotations

import argparse
import os
import sys
import threading
from typing import Iterable

import serial
from serial.tools import list_ports


EXIT_CHAR = b"\x1d"  # Ctrl-]
DEFAULT_BAUD = 115200
READ_CHUNK = 256
READ_TIMEOUT = 0.05


def available_ports() -> list:
    """Return serial ports in deterministic device-name order."""
    return sorted(list_ports.comports(), key=lambda port: port.device)


def print_ports(ports: Iterable) -> None:
    """Print available serial devices."""
    ports = list(ports)
    if not ports:
        print("No serial ports found.")
        return

    for port in ports:
        description = port.description or "unknown"
        hwid = port.hwid or "unknown"
        print(f"{port.device:20} {description} [{hwid}]")


def choose_port(requested: str | None) -> str:
    """Resolve an explicitly requested port or auto-select an unambiguous one."""
    if requested:
        return requested

    ports = available_ports()
    if len(ports) == 1:
        return ports[0].device

    # Prefer common USB-CDC naming on Linux/WSL when there is one clear match.
    cdc_candidates = [
        port
        for port in ports
        if port.device.startswith("/dev/ttyACM")
        or "USB" in (port.description or "").upper()
        or "CDC" in (port.description or "").upper()
    ]
    if len(cdc_candidates) == 1:
        return cdc_candidates[0].device

    if not ports:
        raise RuntimeError("no serial ports found; connect the RP2350 or specify PORT")

    print("Multiple serial ports found; specify one explicitly:", file=sys.stderr)
    for port in ports:
        print(f"  {port.device:20} {port.description or 'unknown'}", file=sys.stderr)
    raise RuntimeError("serial port selection is ambiguous")


def serial_to_stdout(ser: serial.Serial, stop_event: threading.Event) -> None:
    """Forward bytes received from RP2350 CDC to host stdout."""
    try:
        while ser.is_open and not stop_event.is_set():
            data = ser.read(READ_CHUNK)
            if data:
                os.write(sys.stdout.fileno(), data)
    except (OSError, serial.SerialException):
        stop_event.set()


def run_posix_console(ser: serial.Serial, stop_event: threading.Event) -> None:
    """Run immediate single-byte input on POSIX terminals."""
    import termios
    import tty

    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)

    try:
        tty.setraw(fd)
        while not stop_event.is_set():
            data = os.read(fd, 1)
            if not data or data == EXIT_CHAR:
                break
            ser.write(data)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)


def run_windows_console(ser: serial.Serial, stop_event: threading.Event) -> None:
    """Run immediate single-byte input on the native Windows console."""
    import msvcrt

    while not stop_event.is_set():
        data = msvcrt.getch()
        if not data or data == EXIT_CHAR:
            break

        # Ignore the prefix byte of Windows extended-key sequences.
        if data in (b"\x00", b"\xe0"):
            msvcrt.getch()
            continue

        ser.write(data)


def run_stream_console(ser: serial.Serial, stop_event: threading.Event) -> None:
    """Forward stdin when it is redirected rather than attached to a TTY."""
    while not stop_event.is_set():
        data = sys.stdin.buffer.read(1)
        if not data or data == EXIT_CHAR:
            break
        ser.write(data)


def run_console(port: str, baud: int) -> int:
    """Open the CDC endpoint and run the bidirectional console bridge."""
    try:
        ser = serial.Serial(port, baud, timeout=READ_TIMEOUT)
    except serial.SerialException as exc:
        print(f"error: cannot open {port}: {exc}", file=sys.stderr)
        return 1

    stop_event = threading.Event()
    rx_thread = threading.Thread(
        target=serial_to_stdout,
        args=(ser, stop_event),
        daemon=True,
    )
    rx_thread.start()

    try:
        if not sys.stdin.isatty():
            run_stream_console(ser, stop_event)
        elif os.name == "nt":
            run_windows_console(ser, stop_event)
        elif os.name == "posix":
            run_posix_console(ser, stop_event)
        else:
            run_stream_console(ser, stop_event)
    except (KeyboardInterrupt, OSError, serial.SerialException):
        pass
    finally:
        stop_event.set()
        ser.close()
        rx_thread.join(timeout=0.2)

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="pi86 RP2350 USB CDC stdin/stdout console bridge"
    )
    parser.add_argument(
        "port",
        nargs="?",
        help="serial device, for example /dev/ttyACM0 or COM5; auto-detected if omitted",
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=(
            f"serial baud rate (default: {DEFAULT_BAUD}; normally ignored by USB CDC)"
        ),
    )
    parser.add_argument(
        "-l",
        "--list",
        action="store_true",
        help="list serial ports and exit",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.list:
        print_ports(available_ports())
        return 0

    try:
        port = choose_port(args.port)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(f"pi86 console: {port} @ {args.baud} (Ctrl-] to exit)", file=sys.stderr)
    return run_console(port, args.baud)


if __name__ == "__main__":
    raise SystemExit(main())
