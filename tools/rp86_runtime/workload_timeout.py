"""RP2350-owned wall-clock execution limit, distinct from Host I/O timeout."""

from dataclasses import dataclass
from decimal import Decimal, DecimalException
import struct

from .protocol import (
    Message, STATUS_OK, STATUS_BAD_STATE,
    TYPE_WORKLOAD_TIMEOUT_REQUEST, TYPE_WORKLOAD_TIMEOUT_RESULT,
    WORKLOAD_TIMEOUT_GET, WORKLOAD_TIMEOUT_SET, WORKLOAD_TIMEOUT_MAX_MS,
)

_REQUEST = struct.Struct("<II")
_STATUS = struct.Struct("<13I")


def parse_timeout(arguments: tuple[str, ...]) -> int | None:
    if not arguments:
        return None
    if len(arguments) != 1:
        raise ValueError("usage: timeout [seconds|off]")
    if arguments[0].lower() == "off":
        return 0
    try:
        value = Decimal(arguments[0]) * 1000
        if (not value.is_finite() or not 1 <= value <= WORKLOAD_TIMEOUT_MAX_MS
                or value != value.to_integral_value()):
            raise ValueError("timeout must be 0.001–86400 seconds in whole milliseconds, or off")
        return int(value)
    except DecimalException as exc:
        raise ValueError("timeout requires seconds or off") from exc


def timeout_request(timeout_ms: int | None, sequence: int) -> Message:
    if timeout_ms is not None and not 0 <= timeout_ms <= WORKLOAD_TIMEOUT_MAX_MS:
        raise ValueError("workload timeout is outside the supported range")
    return Message(TYPE_WORKLOAD_TIMEOUT_REQUEST, sequence, _REQUEST.pack(
        WORKLOAD_TIMEOUT_GET if timeout_ms is None else WORKLOAD_TIMEOUT_SET,
        timeout_ms or 0,
    ))


@dataclass(frozen=True)
class WorkloadTimeout:
    timeout_ms: int
    remaining_ms: int
    workload_id: int
    boot_id: int
    armed: int

    @classmethod
    def from_reply(cls, reply: Message, request: Message) -> "WorkloadTimeout":
        if reply.message_type != TYPE_WORKLOAD_TIMEOUT_RESULT or reply.sequence != request.sequence:
            raise ValueError("workload timeout reply type/sequence mismatch")
        if reply.status != STATUS_OK:
            if reply.status == STATUS_BAD_STATE:
                raise ValueError("execution limit is unavailable for an active prepared workload")
            raise ValueError(f"workload timeout status={reply.status}")
        if len(reply.payload) != _STATUS.size:
            raise ValueError("workload timeout payload must be exactly 52 bytes")
        values = _STATUS.unpack(reply.payload)
        if any(values[5:]):
            raise ValueError("workload timeout reserved fields must be zero")
        result = cls(*values[:5])
        if (result.timeout_ms > WORKLOAD_TIMEOUT_MAX_MS or result.armed not in (0, 1)
                or result.remaining_ms > result.timeout_ms
                or (not result.armed and result.remaining_ms != 0)
                or (result.armed and not all((result.timeout_ms, result.workload_id, result.boot_id)))):
            raise ValueError("inconsistent workload timeout status")
        operation, value = _REQUEST.unpack(request.payload)
        if operation == WORKLOAD_TIMEOUT_SET and result.timeout_ms != value:
            raise ValueError("workload timeout setting was not applied")
        return result

    def format(self) -> str:
        if self.timeout_ms == 0:
            return "Workload execution limit: OFF (RP2350 supervised)"
        state = f"armed, remaining={self.remaining_ms / 1000:.3f}s" if self.armed else "not armed"
        return (f"Workload execution limit: {self.timeout_ms / 1000:.3f}s ({state})\n"
                "  Deadline is measured from each run/restart, not from the last query.")
