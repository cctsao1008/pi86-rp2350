"""Host access to processor-visible Internal SRAM."""

from __future__ import annotations

import struct

from .protocol import (
    MEMORY_DATA_BYTES,
    MEMORY_READ,
    MEMORY_WRITE,
    Message,
    STATUS_OK,
    TYPE_MEMORY_REQUEST,
    TYPE_MEMORY_RESULT,
)

_HEADER = struct.Struct("<B3xII")


def _address(value: int) -> int:
    if not 0 <= value <= 0x3FFFF:
        raise ValueError("Internal SRAM address must be within 0x00000-0x3FFFF")
    return value


def memory_read_request(address: int, length: int, sequence: int) -> Message:
    _address(address)
    if not 1 <= length <= MEMORY_DATA_BYTES or address + length > 0x40000:
        raise ValueError(f"memory read must contain 1-{MEMORY_DATA_BYTES} bytes")
    return Message(TYPE_MEMORY_REQUEST, sequence,
                   _HEADER.pack(MEMORY_READ, address, length))


def memory_write_records(address: int, data: bytes,
                         first_sequence: int) -> list[Message]:
    _address(address)
    if not data or address + len(data) > 0x40000:
        raise ValueError("memory write is empty or outside Internal SRAM")
    records: list[Message] = []
    offset = 0
    sequence = first_sequence
    while offset < len(data):
        chunk = data[offset:offset + MEMORY_DATA_BYTES]
        records.append(Message(
            TYPE_MEMORY_REQUEST, sequence,
            _HEADER.pack(MEMORY_WRITE, address + offset, len(chunk)) + chunk,
        ))
        offset += len(chunk)
        sequence = (sequence + 1) & 0xFFFFFFFF or 1
    return records


def validate_memory_reply(reply: Message, request: Message) -> bytes:
    if reply.message_type != TYPE_MEMORY_RESULT:
        raise ValueError(f"unexpected memory reply type: {reply.message_type}")
    if reply.sequence != request.sequence:
        raise ValueError(
            f"memory reply sequence mismatch: {reply.sequence} != {request.sequence}"
        )
    if reply.status != STATUS_OK:
        raise ValueError(f"memory request failed with status {reply.status}")
    if len(reply.payload) < _HEADER.size:
        raise ValueError("memory reply is truncated")
    if reply.payload[:_HEADER.size] != request.payload[:_HEADER.size]:
        raise ValueError("memory operation/address/length echo mismatch")
    return reply.payload[_HEADER.size:]


def parse_memory_read(reply: Message, request: Message) -> bytes:
    data = validate_memory_reply(reply, request)
    operation, _address_value, length = _HEADER.unpack_from(reply.payload)
    if operation != MEMORY_READ or len(data) != length:
        raise ValueError("memory read reply length mismatch")
    return data


def format_memory_dump(address: int, data: bytes) -> str:
    lines: list[str] = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        hexadecimal = " ".join(f"{byte:02X}" for byte in chunk)
        text = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
        lines.append(f"{address + offset:05X}  {hexadecimal:<47}  |{text}|")
    return "\n".join(lines)
