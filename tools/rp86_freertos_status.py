#!/usr/bin/env python3
"""Read stable FreeRTOS system telemetry snapshots through an active RP86 broker."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import secrets
import sys
import time

from rp86_runtime.broker import BrokerClient, discover_brokers, select_broker
from rp86_runtime.freertos_system import (
    FreeRTOSSystemTelemetry,
    read_stable_telemetry,
    sustained_progress,
    telemetry_address_from_map,
)
from rp86_runtime.memory import memory_read_request, parse_memory_read
from rp86_runtime.protocol import Message


def _parse_address(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid address: {value}") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Read the #67 FreeRTOS RAM-backed telemetry witness."
    )
    parser.add_argument(
        "address", nargs="?", type=_parse_address,
        help="physical address of gRp86Telemetry from the linker map",
    )
    parser.add_argument(
        "--map", type=Path,
        help="Watcom linker map containing _gRp86Telemetry",
    )
    parser.add_argument("--hid-serial", help="select one active RP86 broker by device ID")
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument(
        "--samples", type=int, default=1,
        help="number of stable snapshots to read (default: 1)",
    )
    parser.add_argument(
        "--interval", type=float, default=1.0,
        help="seconds between samples when --samples is greater than 1",
    )
    parser.add_argument(
        "--verify-progress", action="store_true",
        help="require LED, queue, and event counters to advance with zero errors",
    )
    args = parser.parse_args(argv)

    if (args.address is None) == (args.map is None):
        parser.error("supply exactly one of ADDRESS or --map FILE")
    if args.samples < 1:
        parser.error("--samples must be at least 1")
    if args.interval < 0:
        parser.error("--interval must not be negative")
    if args.verify_progress and args.samples < 2:
        parser.error("--verify-progress requires at least two samples")

    try:
        address = args.address
        if address is None:
            address = telemetry_address_from_map(
                args.map.read_text(encoding="utf-8", errors="replace")
            )
        record = select_broker(discover_brokers(), args.hid_serial)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"telemetry: {exc}", file=sys.stderr)
        return 2
    if record is None:
        print("telemetry: no active RP86 broker; start the RP86 runtime first",
              file=sys.stderr)
        return 2

    client = BrokerClient(record, f"freertos-telemetry-{os.getpid()}")
    sequence = secrets.randbits(31) or 1

    def read_memory(read_address: int, length: int) -> bytes:
        nonlocal sequence
        request = memory_read_request(read_address, length, sequence)
        request_id = f"telemetry-{sequence}"
        sequence = (sequence + 1) & 0xFFFFFFFF or 1
        response = client.exchange(request.encode(), request_id, args.timeout)
        if not response.get("ok"):
            raise RuntimeError(str(response.get("error") or "broker exchange failed"))
        try:
            reply = Message.decode(bytes.fromhex(str(response["reply_hex"])))
            return parse_memory_read(reply, request)
        except (KeyError, ValueError) as exc:
            raise RuntimeError(f"invalid broker memory reply: {exc}") from exc

    snapshots: list[FreeRTOSSystemTelemetry] = []
    try:
        for index in range(args.samples):
            snapshot = read_stable_telemetry(read_memory, address)
            snapshots.append(snapshot)
            if args.samples > 1:
                print(f"Sample {index + 1}/{args.samples}")
            print(snapshot.format(address))
            if index + 1 < args.samples:
                print()
                time.sleep(args.interval)
    except (RuntimeError, ValueError) as exc:
        print(f"telemetry: {exc}", file=sys.stderr)
        return 2

    if args.verify_progress:
        passed, failures = sustained_progress(snapshots[0], snapshots[-1])
        print()
        if passed:
            print("SUSTAINED PROGRESS: PASS")
            return 0
        print("SUSTAINED PROGRESS: FAIL")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
