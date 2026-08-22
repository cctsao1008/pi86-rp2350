"""Provider-neutral 64-byte pi86-rp2350 AI Bridge protocol."""

from __future__ import annotations

from dataclasses import dataclass
import struct

PROTOCOL_VERSION = 1
PAYLOAD_SIZE = 52
MESSAGE_SIZE = 64
_MESSAGE = struct.Struct("<BBHIHH52s")

TYPE_HELLO = 1
TYPE_TEXT = 2
TYPE_ACK = 3
TYPE_ERROR = 0x7F


@dataclass(frozen=True)
class Message:
    message_type: int
    sequence: int
    payload: bytes = b""
    flags: int = 0
    status: int = 0
    version: int = PROTOCOL_VERSION

    def encode(self) -> bytes:
        if self.version != PROTOCOL_VERSION:
            raise ValueError(f"unsupported protocol version: {self.version}")
        if not 0 <= len(self.payload) <= PAYLOAD_SIZE:
            raise ValueError(f"payload exceeds {PAYLOAD_SIZE} bytes")
        return _MESSAGE.pack(
            self.version,
            self.message_type,
            self.flags,
            self.sequence,
            len(self.payload),
            self.status,
            self.payload.ljust(PAYLOAD_SIZE, b"\0"),
        )

    @classmethod
    def decode(cls, record: bytes) -> "Message":
        if len(record) != MESSAGE_SIZE:
            raise ValueError(f"record must be exactly {MESSAGE_SIZE} bytes")
        version, message_type, flags, sequence, length, status, payload = (
            _MESSAGE.unpack(record)
        )
        if version != PROTOCOL_VERSION:
            raise ValueError(f"unsupported protocol version: {version}")
        if length > PAYLOAD_SIZE:
            raise ValueError(f"invalid payload length: {length}")
        return cls(
            message_type=message_type,
            sequence=sequence,
            payload=payload[:length],
            flags=flags,
            status=status,
            version=version,
        )


assert _MESSAGE.size == MESSAGE_SIZE
