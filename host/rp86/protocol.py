"""Provider-neutral 64-byte RP86 Host Protocol."""

from __future__ import annotations

from dataclasses import dataclass
import struct

PROTOCOL_VERSION = 1
PAYLOAD_SIZE = 52
MESSAGE_SIZE = 64
_MESSAGE = struct.Struct("<BBHIHH52s")
NATIVE_WITNESS_MAGIC = b"P86N"
NATIVE_WITNESS_VERSION = 1
_NATIVE_WITNESS = struct.Struct("<4sBBHIII")
NATIVE_PROCESSOR_MASK = 0x0003
NATIVE_PROCESSOR_INTEL_8086 = 0x0001
NATIVE_PROCESSOR_NEC_V30 = 0x0002
NATIVE_PROCESSOR_NAMES = {
    NATIVE_PROCESSOR_INTEL_8086: "intel-8086",
    NATIVE_PROCESSOR_NEC_V30: "nec-v30",
}

TYPE_HELLO = 1
TYPE_TEXT = 2
TYPE_ACK = 3
TYPE_COMMAND = 4
TYPE_RESULT = 5
TYPE_HEARTBEAT = 6
TYPE_WORKLOAD_BEGIN = 0x20
TYPE_WORKLOAD_DATA = 0x21
TYPE_WORKLOAD_COMMIT = 0x22
TYPE_WORKLOAD_CONTROL = 0x23
TYPE_WORKLOAD_STATUS = 0x24
TYPE_WORKLOAD_RESULT = 0x25
TYPE_WORKLOAD_TIMEOUT_REQUEST = 0x26
TYPE_WORKLOAD_TIMEOUT_RESULT = 0x27
TYPE_RUNTIME_CONTROL = 0x30
TYPE_RUNTIME_STATUS = 0x31
TYPE_FILESYSTEM_REQUEST = 0x40
TYPE_FILESYSTEM_RESULT = 0x41
TYPE_MEMORY_REQUEST = 0x50
TYPE_MEMORY_RESULT = 0x51
TYPE_DIAGNOSTICS_REQUEST = 0x60
TYPE_DIAGNOSTICS_RESULT = 0x61
TYPE_ERROR = 0x7F

FLAG_RETRY = 1 << 0

STATUS_OK = 0
STATUS_BAD_VERSION = 1
STATUS_BAD_LENGTH = 2
STATUS_BUSY = 3
STATUS_TIMEOUT = 4
STATUS_BAD_SEQUENCE = 5
STATUS_SERVICE_UNAVAILABLE = 6
STATUS_BAD_CRC = 7
STATUS_BAD_STATE = 8
STATUS_BAD_WORKLOAD = 9
STATUS_IO_ERROR = 10
STATUS_NOT_FOUND = 11
STATUS_INVALID_PATH = 12
STATUS_NO_SPACE = 13

WORKLOAD_CONTROL_RUN = 1
WORKLOAD_CONTROL_STOP = 2
WORKLOAD_CONTROL_RESTART = 3
WORKLOAD_CONTROL_STATUS = 4

WORKLOAD_TIMEOUT_GET = 0
WORKLOAD_TIMEOUT_SET = 1
WORKLOAD_TIMEOUT_MAX_MS = 86400000

RUNTIME_CONTROL_ENTER_BOOTLOADER = 1
RUNTIME_CONTROL_SELFTEST = 2
RUNTIME_CONTROL_REBOOT = 3

FILESYSTEM_LIST = 1
FILESYSTEM_DF = 2
FILESYSTEM_READ = 3
FILESYSTEM_WRITE_BEGIN = 4
FILESYSTEM_WRITE_DATA = 5
FILESYSTEM_WRITE_COMMIT = 6

MEMORY_READ = 1
MEMORY_WRITE = 2
MEMORY_DATA_BYTES = 40

FILESYSTEM_FLAG_EOF = 1 << 0
FILESYSTEM_FLAG_DIRECTORY = 1 << 1
FILESYSTEM_FLAG_TRUNCATED = 1 << 2
FILESYSTEM_READ_DATA_BYTES = 40
FILESYSTEM_WRITE_DATA_BYTES = 40
FILESYSTEM_LIST_NAME_BYTES = 42
FILESYSTEM_READ_PATH_BYTES = 44
FILESYSTEM_WRITE_PATH_BYTES = 36


@dataclass(frozen=True)
class NativeServiceWitness:
    """Processor-owned counters observed from one committed native ISR reply."""

    service_type: int
    boot_id: int
    cpu_sequence: int
    command_sequence: int
    text: bytes
    flags: int = 0
    version: int = NATIVE_WITNESS_VERSION

    @property
    def processor(self) -> str | None:
        """Identity produced by the physical processor's AAD 16 behavior."""
        return NATIVE_PROCESSOR_NAMES.get(self.flags & NATIVE_PROCESSOR_MASK)

    def encode(self) -> bytes:
        payload = _NATIVE_WITNESS.pack(
            NATIVE_WITNESS_MAGIC,
            self.version,
            self.service_type,
            self.flags,
            self.boot_id,
            self.cpu_sequence,
            self.command_sequence,
        ) + self.text
        if len(payload) > PAYLOAD_SIZE:
            raise ValueError("native service witness exceeds bridge payload")
        return payload

    @classmethod
    def decode(cls, payload: bytes) -> "NativeServiceWitness":
        if len(payload) < _NATIVE_WITNESS.size:
            raise ValueError("native service witness is truncated")
        magic, version, service_type, flags, boot_id, cpu_seq, command_seq = (
            _NATIVE_WITNESS.unpack_from(payload)
        )
        if magic != NATIVE_WITNESS_MAGIC:
            raise ValueError("native service witness magic mismatch")
        if version != NATIVE_WITNESS_VERSION:
            raise ValueError(
                f"unsupported native service witness version: {version}"
            )
        return cls(
            service_type=service_type,
            boot_id=boot_id,
            cpu_sequence=cpu_seq,
            command_sequence=command_seq,
            text=payload[_NATIVE_WITNESS.size:],
            flags=flags,
            version=version,
        )


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
