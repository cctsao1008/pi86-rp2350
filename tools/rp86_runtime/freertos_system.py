"""Decode the small RAM-backed FreeRTOS system telemetry witness."""

from __future__ import annotations

from dataclasses import dataclass
import re
import struct


TELEMETRY_SIZE = 16
TELEMETRY_SEQUENCE_OFFSET = 8
_TELEMETRY = struct.Struct("<8H")
_TELEMETRY_SYMBOL = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\+?\s+_gRp86Telemetry\b",
    re.MULTILINE,
)
_EVENT_NAMES = {
    1: "BOOT",
    2: "LED_ON",
    3: "LED_OFF",
    4: "QUEUE_SEND",
    5: "QUEUE_RECV",
    6: "ERROR",
}


def stable_sequence(sequence: int) -> bool:
    """Return whether the processor-published seqlock is stable."""
    return (sequence & 1) == 0


def decode_sequence(data: bytes) -> int:
    if len(data) != 2:
        raise ValueError("telemetry sequence must be exactly 2 bytes")
    return struct.unpack("<H", data)[0]


def telemetry_address_from_map(map_text: str) -> int:
    """Resolve the workload-local telemetry physical address from a Watcom map."""
    match = _TELEMETRY_SYMBOL.search(map_text)
    if match is None:
        raise ValueError("linker map does not contain _gRp86Telemetry")
    segment = int(match.group(1), 16)
    offset = int(match.group(2), 16)
    return (segment << 4) + offset


@dataclass(frozen=True)
class FreeRTOSSystemTelemetry:
    led_state: int
    led_toggle_count: int
    queue_tx_count: int
    queue_rx_count: int
    event_sequence: int
    event: int
    event_arg: int
    error_count: int

    @classmethod
    def decode(cls, data: bytes) -> "FreeRTOSSystemTelemetry":
        if len(data) != TELEMETRY_SIZE:
            raise ValueError(
                f"FreeRTOS telemetry must be exactly {TELEMETRY_SIZE} bytes"
            )
        return cls(*_TELEMETRY.unpack(data))

    @property
    def event_name(self) -> str:
        return _EVENT_NAMES.get(self.event, f"EVENT_{self.event}")

    @property
    def stable(self) -> bool:
        return stable_sequence(self.event_sequence)

    def format(self, address: int | None = None) -> str:
        heading = "FreeRTOS system telemetry"
        if address is not None:
            heading += f" @ 0x{address:05X}"
        led = "ON" if self.led_state else "OFF"
        return "\n".join(
            (
                heading,
                f"  LED          {led}",
                f"  Toggle count {self.led_toggle_count}",
                f"  Queue TX     {self.queue_tx_count}",
                f"  Queue RX     {self.queue_rx_count}",
                f"  Last event   {self.event_name} arg={self.event_arg} seq={self.event_sequence}",
                f"  Errors       {self.error_count}",
            )
        )
