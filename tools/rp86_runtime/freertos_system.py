"""Decode the RAM-backed FreeRTOS system and scheduler-port witnesses."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import re
import struct


TELEMETRY_SIZE = 16
TELEMETRY_SEQUENCE_OFFSET = 8
PORT_TRACE_SIZE = 14
QUEUE_TRACE_SIZE = 6
_TELEMETRY = struct.Struct("<8H")
_PORT_TRACE = struct.Struct("<7H")
_QUEUE_TRACE = struct.Struct("<3H")
_TELEMETRY_SYMBOL = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\+?\s+_gRp86Telemetry\b",
    re.MULTILINE,
)
_PORT_TRACE_SYMBOL = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\+?\s+_gRp86PortTrace\b",
    re.MULTILINE,
)
_QUEUE_TRACE_SYMBOL = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\+?\s+_gRp86QueueTrace\b",
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
_PORT_STAGE_NAMES = {
    0: "IDLE",
    1: "YIELD_ISR_ENTERED",
    2: "CURRENT_CONTEXT_SAVED",
    3: "SCHEDULER_RETURNED",
    4: "NEXT_SP_LOADED",
    5: "PRE_IRET",
}
_QUEUE_STAGE_NAMES = {
    0: "IDLE",
    1: "ARMED",
    2: "ENTER_XQUEUE_RECEIVE",
    3: "ENTER_VTASK_SUSPEND_ALL",
    4: "BLOCKING_ON_QUEUE_RECEIVE",
    5: "ENTER_XTASK_RESUME_ALL",
    6: "RETURN_XTASK_RESUME_ALL",
    7: "RETURN_XQUEUE_RECEIVE",
}


def stable_sequence(sequence: int) -> bool:
    """Return whether the processor-published seqlock is stable."""
    return (sequence & 1) == 0


def decode_sequence(data: bytes) -> int:
    if len(data) != 2:
        raise ValueError("telemetry sequence must be exactly 2 bytes")
    return struct.unpack("<H", data)[0]


def _physical_address_from_symbol(
    map_text: str,
    pattern: re.Pattern[str],
    symbol: str,
) -> int:
    match = pattern.search(map_text)
    if match is None:
        raise ValueError(f"linker map does not contain {symbol}")
    segment = int(match.group(1), 16)
    offset = int(match.group(2), 16)
    return (segment << 4) + offset


def telemetry_address_from_map(map_text: str) -> int:
    """Resolve the workload-local telemetry physical address from a Watcom map."""
    return _physical_address_from_symbol(
        map_text, _TELEMETRY_SYMBOL, "_gRp86Telemetry"
    )


def port_trace_address_from_map(map_text: str) -> int:
    """Resolve the 8086 FreeRTOS port trace physical address from a Watcom map."""
    return _physical_address_from_symbol(
        map_text, _PORT_TRACE_SYMBOL, "_gRp86PortTrace"
    )


def queue_trace_address_from_map(map_text: str) -> int:
    """Resolve the queue-blocking trace physical address from a Watcom map."""
    return _physical_address_from_symbol(
        map_text, _QUEUE_TRACE_SYMBOL, "_gRp86QueueTrace"
    )


def counter_delta(previous: int, current: int) -> int:
    """Return one unsigned 16-bit forward counter delta."""
    return (current - previous) & 0xFFFF


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


@dataclass(frozen=True)
class FreeRTOSPortTrace:
    stage: int
    old_ss: int
    old_sp: int
    old_tcb: int
    new_ss: int
    new_sp: int
    new_tcb: int

    @classmethod
    def decode(cls, data: bytes) -> "FreeRTOSPortTrace":
        if len(data) != PORT_TRACE_SIZE:
            raise ValueError(
                f"FreeRTOS port trace must be exactly {PORT_TRACE_SIZE} bytes"
            )
        return cls(*_PORT_TRACE.unpack(data))

    @property
    def stage_name(self) -> str:
        return _PORT_STAGE_NAMES.get(self.stage, f"STAGE_{self.stage}")

    @property
    def same_stack_segment(self) -> bool:
        return self.old_ss == self.new_ss

    def format(self, address: int | None = None) -> str:
        heading = "FreeRTOS port switch trace"
        if address is not None:
            heading += f" @ 0x{address:05X}"
        ss_relation = "same" if self.same_stack_segment else "CHANGED"
        return "\n".join(
            (
                heading,
                f"  Stage        {self.stage} ({self.stage_name})",
                f"  Outgoing     SS:SP={self.old_ss:04X}:{self.old_sp:04X} TCB={self.old_tcb:04X}",
                f"  Incoming     SS:SP={self.new_ss:04X}:{self.new_sp:04X} TCB={self.new_tcb:04X}",
                f"  Stack SS     {ss_relation}",
            )
        )


@dataclass(frozen=True)
class FreeRTOSQueueTrace:
    armed: int
    stage: int
    detail: int

    @classmethod
    def decode(cls, data: bytes) -> "FreeRTOSQueueTrace":
        if len(data) != QUEUE_TRACE_SIZE:
            raise ValueError(
                f"FreeRTOS queue trace must be exactly {QUEUE_TRACE_SIZE} bytes"
            )
        return cls(*_QUEUE_TRACE.unpack(data))

    @property
    def stage_name(self) -> str:
        return _QUEUE_STAGE_NAMES.get(self.stage, f"STAGE_{self.stage}")

    def format(self, address: int | None = None) -> str:
        heading = "FreeRTOS queue blocking trace"
        if address is not None:
            heading += f" @ 0x{address:05X}"
        return "\n".join(
            (
                heading,
                f"  Armed        {self.armed}",
                f"  Stage        {self.stage} ({self.stage_name})",
                f"  Detail       0x{self.detail:04X} ({self.detail})",
            )
        )


def read_stable_telemetry(
    read_memory: Callable[[int, int], bytes],
    address: int,
    *,
    attempts: int = 5,
) -> FreeRTOSSystemTelemetry:
    """Read one coherent snapshot using the workload's odd/even sequence."""
    if attempts < 1:
        raise ValueError("telemetry attempts must be at least 1")
    for _attempt in range(attempts):
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
            return snapshot
    raise RuntimeError("processor state changed during every telemetry snapshot")


def read_port_trace(
    read_memory: Callable[[int, int], bytes],
    address: int,
) -> FreeRTOSPortTrace:
    """Read the one-shot first-yield scheduler-port witness."""
    return FreeRTOSPortTrace.decode(read_memory(address, PORT_TRACE_SIZE))


def read_queue_trace(
    read_memory: Callable[[int, int], bytes],
    address: int,
) -> FreeRTOSQueueTrace:
    """Read the first xQueueReceive blocking-path witness."""
    return FreeRTOSQueueTrace.decode(read_memory(address, QUEUE_TRACE_SIZE))


def sustained_progress(
    first: FreeRTOSSystemTelemetry,
    last: FreeRTOSSystemTelemetry,
) -> tuple[bool, tuple[str, ...]]:
    """Evaluate the #71 long-running workload's minimal forward-progress witness."""
    failures: list[str] = []
    if first.error_count != 0 or last.error_count != 0:
        failures.append("error_count is non-zero")
    if counter_delta(first.led_toggle_count, last.led_toggle_count) == 0:
        failures.append("LED toggle count did not advance")
    if counter_delta(first.queue_tx_count, last.queue_tx_count) == 0:
        failures.append("queue TX count did not advance")
    if counter_delta(first.queue_rx_count, last.queue_rx_count) == 0:
        failures.append("queue RX count did not advance")
    if counter_delta(first.event_sequence, last.event_sequence) == 0:
        failures.append("event sequence did not advance")
    return not failures, tuple(failures)
