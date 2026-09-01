"""Typed Host view of RP2350 workload and processor execution state."""

from __future__ import annotations

from dataclasses import dataclass

from .protocol import NativeServiceWitness

from .workload import (
    CLOCK_MODES,
    CLOCK_MODE_NAMES,
    PROCESSOR_FLAG_IDLE,
    PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
    decode_status_payload,
)


WORKLOAD_STATE_NAMES = {
    0: "EMPTY",
    1: "RECEIVING",
    2: "STAGED",
    3: "RUNNING",
    4: "STOPPED",
    5: "COMPLETED",
    6: "FAULTED",
    7: "TIMED_OUT",
}


@dataclass
class RequestSequence:
    value: int = 1

    def __post_init__(self) -> None:
        self.value &= 0xFFFFFFFF
        if self.value == 0:
            self.value = 1

    def advance_after(self, sequence: int) -> int:
        self.value = (sequence + 1) & 0xFFFFFFFF or 1
        return self.value


@dataclass
class ProcessorObservationState:
    """Latest native-processor evidence observed by the Host session."""

    boot_id: int | None = None
    cpu_sequence: int | None = None
    command_sequence: int | None = None
    processor: str | None = None
    connected: bool = False
    result_pass_seen: bool = False

    def accept_witness(self, witness: NativeServiceWitness) -> None:
        self.boot_id = witness.boot_id
        self.cpu_sequence = witness.cpu_sequence
        self.command_sequence = witness.command_sequence
        self.processor = witness.processor
        self.connected = True

    def mark_disconnected(self) -> None:
        self.connected = False


def workload_state_name(state: int) -> str:
    return WORKLOAD_STATE_NAMES.get(state, f"UNKNOWN({state})")


def processor_execution_state(clock_mode: int, processor_flags: int) -> str:
    if processor_flags & PROCESSOR_FLAG_IDLE:
        return "IDLE / HLT"
    if clock_mode == CLOCK_MODES["stopped"]:
        return "STOPPED / RESET"
    return "ACTIVE"


def workload_upload_requires_stop(workload_state: int) -> bool:
    return workload_state in (3, 5)


def prepared_runtime_is_available(
    clock_mode: int,
    workload_state: int = 0,
    processor_flags: int = PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
) -> bool:
    return (
        workload_state == 0
        and bool(
            processor_flags & PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED
        )
        and clock_mode in (CLOCK_MODES["auto"], CLOCK_MODES["free-running"])
    )


@dataclass
class WorkloadRuntimeState:
    workload_id: int = 0
    lifecycle: int = 0
    detail: int = 0
    clock_mode: int = 0
    cycles: int = 0
    processor_flags: int = 0

    @classmethod
    def from_payload(cls, payload: bytes) -> "WorkloadRuntimeState":
        return cls(*decode_status_payload(payload))

    def update_from_payload(self, payload: bytes) -> None:
        updated = self.from_payload(payload)
        self.workload_id = updated.workload_id
        self.lifecycle = updated.lifecycle
        self.detail = updated.detail
        self.clock_mode = updated.clock_mode
        self.cycles = updated.cycles
        self.processor_flags = updated.processor_flags

    @property
    def lifecycle_name(self) -> str:
        return workload_state_name(self.lifecycle)

    @property
    def clock_name(self) -> str:
        return CLOCK_MODE_NAMES.get(
            self.clock_mode, f"UNKNOWN({self.clock_mode})"
        )

    @property
    def processor_state(self) -> str:
        return processor_execution_state(self.clock_mode, self.processor_flags)

    @property
    def prepared_runtime_available(self) -> bool:
        return prepared_runtime_is_available(
            self.clock_mode, self.lifecycle, self.processor_flags
        )

    @property
    def upload_requires_stop(self) -> bool:
        return workload_upload_requires_stop(self.lifecycle)

    @property
    def completed(self) -> bool:
        return self.lifecycle == 5

    def mark_idle(self) -> None:
        self.processor_flags |= PROCESSOR_FLAG_IDLE

    def mark_prepared_runtime_initialized(self) -> None:
        self.processor_flags |= PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED
