#!/usr/bin/env python3
"""pi86 RP2350 USB CDC console bridge.

Bridges the host terminal stdin/stdout to the RP2350 USB CDC serial port.
This is intended to provide a simple host-side console for the pi86 virtual
BIOS and, later, ELKS headless console I/O.

Exit with Ctrl-].
"""

import argparse
import os
import sys
import threading

import serial


EXIT_CHAR = b"\x1d"  # Ctrl-]


def serial_to_stdout(ser: serial.Serial) -> None:
    """Forward bytes received from RP2350 CDC to stdout."""
    while ser.is_open:
        data = ser.read(64)
        if data:
            os.write(sys.stdout.fileno(), data)


def run_console(port: str, baud: int) -> int:
    """Run the interactive CDC console bridge."""
    try:
        ser = serial.Serial(port, baud, timeout=0.05)
    except serial.SerialException as exc:
        print(f"error: cannot open {port}: {exc}", file=sys.stderr)
        return 1

    rx_thread = threading.Thread(
        target=serial_to_stdout,
        args=(ser,),
        daemon=True,
    )
    rx_thread.start()

    if os.name != "posix" or not sys.stdin.isatty():
        try:
            while True:
                data = sys.stdin.buffer.read(1)
                if not data or data == EXIT_CHAR:
                    break
                ser.write(data)
        finally:
            ser.close()
        return 0

    import termios
    import tty

    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)

    try:
        tty.setraw(fd)
        while True:
            data = os.read(fd, 1)
            if not data or data == EXIT_CHAR:
                break
            ser.write(data)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        ser.close()

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="pi86 RP2350 USB CDC stdin/stdout console bridge"
    )
    parser.add_argument(
        "port",
        nargs="?",
        default="/dev/ttyACM0",
        help="serial device (default: /dev/ttyACM0)",
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=115200,
        help="serial baud rate (default: 115200; ignored by USB CDC transport)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return run_console(args.port, args.baud)


if __name__ == "__main__":
    raise SystemExit(main())
