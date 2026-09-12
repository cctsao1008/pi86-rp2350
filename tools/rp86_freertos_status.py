#!/usr/bin/env python3
"""Read stable FreeRTOS system telemetry snapshots through an active RP86 broker."""

from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import secrets
import sys
import time

from rp86_runtime.broker import BrokerClient, discover_brokers, select_broker
from rp86_runtime.freertos_system import (
    FreeRTOSSystemTelemetry,
    port_trace_address_from_map,
    queue_trace_address_from_map,
    read_port_trace,
    read_queue_trace,
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


def _observation_timestamp() -> str:
    """Return a millisecond local timestamp with an explicit UTC offset."""
    now = datetime.now().astimezone()
    offset = now.strftime("%z")
    if len(offset) == 5:
        offset = f"{offset[:3]}:{offset[3:]}"
    return f"{now.strftime('%Y-%m-%d %H:%M:%S')}.{now.microsecond // 1000:03d} {offset}"


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
    parser.add_argument(
        "--port-trace", action="store_true",
        help="also decode _gRp86PortTrace (requires --map)",
    )
    parser.add_argument(
        "--queue-trace", action="store_true",
        help="also decode the first xQueueReceive blocking witness (requires --map)",
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
    if (args.port_trace or args.queue_trace) and args.map is None:
        parser.error("--port-trace/--queue-trace require --map FILE")
    if args.samples < 1:
        parser.error("--samples must be at least 1")
    if args.interval < 0:
        parser.error("--interval must not be negative")
    if args.verify_progress and args.samples < 2:
        parser.error("--verify-progress requires at least two samples")

    try:
        address = args.address
        port_trace_address = None
        queue_trace_address = None
        if args.map is not None:
            map_text = args.map.read_text(encoding="utf-8", errors="replace")
            address = telemetry_address_from_map(map_text)
            if args.port_trace:
                port_trace_address = port_trace_address_from_map(map_text)
            if args.queue_trace:
                queue_trace_address = queue_trace_address_from_map(map_text)
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
            print(f"[{_observation_timestamp()}] {snapshot.format(address)}")
            if index + 1 < args.samples:
                print()
                time.sleep(args.interval)

        if queue_trace_address is not None:
            print()
            queue_trace = read_queue_trace(read_memory, queue_trace_address)
            print(
                f"[{_observation_timestamp()}] "
                f"{queue_trace.format(queue_trace_address)}"
            )

        if port_trace_address is not None:
            print()
            trace = read_port_trace(read_memory, port_trace_address)
            print(f"[{_observation_timestamp()}] {trace.format(port_trace_address)}")
    except (RuntimeError, TimeoutError, ValueError) as exc:
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
