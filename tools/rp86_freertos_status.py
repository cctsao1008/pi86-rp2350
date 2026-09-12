#!/usr/bin/env python3
"""Read one stable FreeRTOS system telemetry snapshot through an active RP86 broker."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import secrets
import sys

from rp86_runtime.broker import BrokerClient, discover_brokers, select_broker
from rp86_runtime.freertos_system import (
    FreeRTOSSystemTelemetry,
    TELEMETRY_SEQUENCE_OFFSET,
    TELEMETRY_SIZE,
    decode_sequence,
    stable_sequence,
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
    args = parser.parse_args(argv)

    if (args.address is None) == (args.map is None):
        parser.error("supply exactly one of ADDRESS or --map FILE")

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

    try:
        for _attempt in range(5):
            before = decode_sequence(
                read_memory(address + TELEMETRY_SEQUENCE_OFFSET, 2)
            )
            if not stable_sequence(before):
                continue
            raw = read_memory(address, TELEMETRY_SIZE)
            after = decode_sequence(
                read_memory(address + TELEMETRY_SEQUENCE_OFFSET, 2)
            )
            snapshot = FreeRTOSSystemTelemetry.decode(raw)
            if before == after == snapshot.event_sequence and snapshot.stable:
                print(snapshot.format(address))
                return 0
    except (RuntimeError, ValueError) as exc:
        print(f"telemetry: {exc}", file=sys.stderr)
        return 2

    print("telemetry: processor state changed during every snapshot; retry",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
