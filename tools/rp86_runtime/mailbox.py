"""Canonical owner-transfer mailbox in processor-visible Internal SRAM."""

from __future__ import annotations

from dataclasses import dataclass
import struct

from .memory import memory_write_records

from .processor_abi import SHARED_MAILBOX_BASE, SHARED_MAILBOX_SIZE

MAILBOX_BASE = SHARED_MAILBOX_BASE
MAILBOX_SIZE = SHARED_MAILBOX_SIZE
MAILBOX_DATA_OFFSET = 32
MAILBOX_DATA_SIZE = MAILBOX_SIZE - MAILBOX_DATA_OFFSET
MAILBOX_MAGIC = b"R86M"
MAILBOX_VERSION = 1
OWNER_HOST = 1
OWNER_PROCESSOR = 2
STATUS_EMPTY = 0
STATUS_REQUEST_READY = 1
STATUS_PROCESSING = 2
STATUS_RESULT_READY = 3
STATUS_ERROR = 4
_HEADER = struct.Struct("<4sHHHHIHHIII")


@dataclass(frozen=True)
class MailboxHeader:
    owner: int
    status: int
    generation: int
    request_length: int
    response_length: int
    flags: int = 0

    @classmethod
    def decode(cls, data: bytes) -> "MailboxHeader":
        if len(data) != _HEADER.size:
            raise ValueError("shared mailbox header is truncated")
        magic, version, size, owner, status, generation, request_length, \
            response_length, flags, reserved0, reserved1 = _HEADER.unpack(data)
        if magic != MAILBOX_MAGIC or version != MAILBOX_VERSION or size != _HEADER.size:
            raise ValueError("shared mailbox header ABI mismatch")
        if reserved0 != 0 or reserved1 != 0:
            raise ValueError("shared mailbox reserved fields are not zero")
        if request_length > MAILBOX_DATA_SIZE or response_length > MAILBOX_DATA_SIZE:
            raise ValueError("shared mailbox length exceeds its data area")
        return cls(owner, status, generation, request_length,
                   response_length, flags)


def mailbox_commit_records(data: bytes, generation: int,
                           first_sequence: int) -> list:
    if not data or len(data) > MAILBOX_DATA_SIZE:
        raise ValueError(f"mailbox request must contain 1-{MAILBOX_DATA_SIZE} bytes")
    # Keep owner=HOST while data and metadata are changing. The final two-byte
    # owner write is the only publication point observed by the processor.
    header = _HEADER.pack(
        MAILBOX_MAGIC, MAILBOX_VERSION, _HEADER.size,
        OWNER_HOST, STATUS_REQUEST_READY, generation,
        len(data), 0, 0, 0, 0,
    )
    records = memory_write_records(
        MAILBOX_BASE + MAILBOX_DATA_OFFSET, data, first_sequence
    )
    sequence = (records[-1].sequence + 1) & 0xFFFFFFFF or 1
    records += memory_write_records(MAILBOX_BASE, header, sequence)
    sequence = (records[-1].sequence + 1) & 0xFFFFFFFF or 1
    records += memory_write_records(
        MAILBOX_BASE + 8, struct.pack("<H", OWNER_PROCESSOR), sequence
    )
    return records


assert _HEADER.size == MAILBOX_DATA_OFFSET
