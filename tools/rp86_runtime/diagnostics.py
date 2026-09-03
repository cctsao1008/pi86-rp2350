"""Read-only, sequence-bound diagnostics of the stopped general executor."""

from dataclasses import asdict, dataclass
import struct

from .protocol import Message, TYPE_DIAGNOSTICS_REQUEST, TYPE_DIAGNOSTICS_RESULT
from .protocol import STATUS_OK, STATUS_BAD_STATE, STATUS_BAD_WORKLOAD, STATUS_SERVICE_UNAVAILABLE
from .workload import COMPLETION_REASONS

_SNAPSHOT = struct.Struct("<13I")
CYCLE_NAMES = {0: "MEM_READ", 1: "MEM_WRITE", 2: "IO_READ", 3: "IO_WRITE",
               4: "INTERRUPT_ACK", 5: "UNSUPPORTED"}
DIAGNOSTICS_CYCLE_VALID = 1 << 0
DIAGNOSTICS_DATA_VALID = 1 << 1
DIAGNOSTICS_NO_CYCLE = 1 << 2
DIAGNOSTICS_UNMAPPED = 1 << 3
DIAGNOSTICS_INVALID_LANE = 1 << 4
DIAGNOSTICS_PAD_MISMATCH = 1 << 5
DIAGNOSTICS_CLOCK_FAILURE = 1 << 6
DIAGNOSTICS_INTERRUPT_ACK = 1 << 7
FAULT_FLAGS = {DIAGNOSTICS_NO_CYCLE: "NO_CYCLE", DIAGNOSTICS_UNMAPPED: "UNMAPPED",
               DIAGNOSTICS_INVALID_LANE: "INVALID_LANE", DIAGNOSTICS_PAD_MISMATCH: "PAD_MISMATCH",
               DIAGNOSTICS_CLOCK_FAILURE: "CLOCK_FAILURE", DIAGNOSTICS_INTERRUPT_ACK: "INTERRUPT_ACK"}


def diagnostics_request(workload_id: int, sequence: int) -> Message:
    return Message(TYPE_DIAGNOSTICS_REQUEST, sequence, struct.pack("<I", workload_id))


@dataclass(frozen=True)
class BusDiagnostics:
    workload_id: int
    boot_id: int
    lifecycle: int
    completion_reason: int
    cycles: int
    last_address: int
    last_data: int
    cycle_type: int
    lanes: int
    flags: int

    @classmethod
    def from_reply(cls, reply: Message, request: Message) -> "BusDiagnostics":
        if reply.message_type != TYPE_DIAGNOSTICS_RESULT or reply.sequence != request.sequence:
            raise ValueError("diagnostics reply type/sequence mismatch")
        if reply.status != STATUS_OK:
            reason = {STATUS_BAD_STATE: "processor is executing; stop before trace",
                      STATUS_BAD_WORKLOAD: "workload changed before diagnostics read",
                      STATUS_SERVICE_UNAVAILABLE: "no stopped general-executor diagnostics available"}
            raise ValueError(reason.get(reply.status, f"diagnostics status={reply.status}"))
        if len(reply.payload) != _SNAPSHOT.size:
            raise ValueError("diagnostics payload must be exactly 52 bytes")
        values = _SNAPSHOT.unpack(reply.payload)
        if any(values[10:]):
            raise ValueError("diagnostics reserved fields must be zero")
        snapshot = cls(*values[:10])
        expected_id, = struct.unpack("<I", request.payload)
        if snapshot.workload_id == 0 or (expected_id and snapshot.workload_id != expected_id):
            raise ValueError("diagnostics workload mismatch")
        if snapshot.lifecycle not in (4, 5, 6, 7):
            raise ValueError("diagnostics did not describe a stopped executor")
        if snapshot.completion_reason not in COMPLETION_REASONS or snapshot.flags & ~0xFF:
            raise ValueError("unknown diagnostics reason or flags")
        if snapshot.flags & DIAGNOSTICS_CYCLE_VALID:
            if snapshot.cycle_type not in CYCLE_NAMES or snapshot.last_address > 0xFFFFF or snapshot.lanes > 3:
                raise ValueError("invalid diagnostics bus cycle")
        elif snapshot.flags & DIAGNOSTICS_DATA_VALID:
            raise ValueError("diagnostics data has no valid cycle")
        if snapshot.last_data > 0xFFFF:
            raise ValueError("invalid diagnostics bus data")
        return snapshot

    def as_dict(self) -> dict:
        document = asdict(self)
        valid = bool(self.flags & DIAGNOSTICS_CYCLE_VALID)
        document.update({
            "source": "firmware_stopped_executor",
            "reason": COMPLETION_REASONS[self.completion_reason],
            "last_address": self.last_address if valid else None,
            "last_data": self.last_data if self.flags & DIAGNOSTICS_DATA_VALID else None,
            "cycle_name": CYCLE_NAMES[self.cycle_type] if valid else None,
            "fault_flags": [name for bit, name in FAULT_FLAGS.items() if self.flags & bit],
        })
        return document

    def format(self) -> str:
        values = self.as_dict()
        address = "unavailable" if values["last_address"] is None else f"0x{self.last_address:05X}"
        data = "unavailable" if values["last_data"] is None else f"0x{self.last_data:04X}"
        return (f"Stopped bus diagnostics: workload={self.workload_id} boot={self.boot_id}\n"
                f"  reason={values['reason']} cycles={self.cycles}\n"
                f"  last observed cycle={values['cycle_name'] or 'unavailable'} address={address} "
                f"lanes={self.lanes} data={data}\n"
                f"  flags={','.join(values['fault_flags']) or 'none'}")
