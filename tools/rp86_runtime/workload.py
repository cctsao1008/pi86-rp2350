"""Flat native 8086-class workload format and fixed-record upload transport."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
import zlib

from .protocol import (
    Message,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_COMMIT,
    TYPE_WORKLOAD_CONTROL,
    TYPE_WORKLOAD_DATA,
    WORKLOAD_CONTROL_RESTART,
    WORKLOAD_CONTROL_RUN,
    WORKLOAD_CONTROL_STATUS,
    WORKLOAD_CONTROL_STOP,
)

MAGIC = 0x57363850  # "P86W" in little endian
FORMAT_VERSION = 1
PROCESSOR_ADDRESS_SPACE_SIZE = 0x100000

FLAG_PERSISTENT = 1 << 0
FLAG_STDIO = 1 << 1
FLAG_SHARED_MEMORY = 1 << 2
FLAG_CLOCK_FREE_RUNNING = 1 << 3
FLAG_CLOCK_STEPPED = 1 << 4

_MANIFEST = struct.Struct("<IHHIIIHHHHIII")
_BEGIN_PREFIX = struct.Struct("<I")
_DATA_PREFIX = struct.Struct("<II")
_COMMIT = struct.Struct("<II")
_CONTROL = struct.Struct("<B3xI")
_STATUS_V1 = struct.Struct("<III")
_STATUS_V2 = struct.Struct("<IIIII")
_STATUS = struct.Struct("<IIIIII")

PROCESSOR_FLAG_IDLE = 1 << 0

CLOCK_MODES = {
    "auto": 0,
    "free-running": 1,
    "clock-stepped": 2,
    "stopped": 3,
}
CLOCK_MODE_NAMES = {value: name.upper() for name, value in CLOCK_MODES.items()}

MANIFEST_SIZE = _MANIFEST.size
HOST_PAYLOAD_SIZE = 52
DATA_BYTES = HOST_PAYLOAD_SIZE - _DATA_PREFIX.size

CONTROL_OPERATIONS = {
    "run": WORKLOAD_CONTROL_RUN,
    "stop": WORKLOAD_CONTROL_STOP,
    "restart": WORKLOAD_CONTROL_RESTART,
    "status": WORKLOAD_CONTROL_STATUS,
}


def _linear(segment: int, offset: int) -> int:
    if not 0 <= segment <= 0xFFFF or not 0 <= offset <= 0xFFFF:
        raise ValueError("segment and offset must be 16-bit values")
    address = (segment << 4) + offset
    if address >= PROCESSOR_ADDRESS_SPACE_SIZE:
        raise ValueError("segment:offset is outside the 20-bit processor address space")
    return address


@dataclass(frozen=True)
class WorkloadManifest:
    image_size: int
    image_crc32: int
    load_address: int
    entry_segment: int
    entry_offset: int
    stack_segment: int = 0
    stack_offset: int = 0
    shared_base: int = 0
    shared_size: int = 0
    flags: int = 0

    @classmethod
    def for_image(
        cls,
        image: bytes,
        *,
        load_address: int,
        entry_segment: int,
        entry_offset: int,
        stack_segment: int = 0,
        stack_offset: int = 0,
        shared_base: int = 0,
        shared_size: int = 0,
        flags: int = 0,
    ) -> "WorkloadManifest":
        return cls(
            len(image),
            zlib.crc32(image),
            load_address,
            entry_segment,
            entry_offset,
            stack_segment,
            stack_offset,
            shared_base,
            shared_size,
            flags,
        ).validated()

    def validated(self) -> "WorkloadManifest":
        if self.image_size <= 0:
            raise ValueError("workload image is empty")
        if not 0 <= self.image_crc32 <= 0xFFFFFFFF:
            raise ValueError("image CRC is not a 32-bit value")
        if not 0 <= self.load_address < PROCESSOR_ADDRESS_SPACE_SIZE:
            raise ValueError("load address is outside the 20-bit processor address space")
        image_end = self.load_address + self.image_size
        if image_end > PROCESSOR_ADDRESS_SPACE_SIZE:
            raise ValueError("workload image crosses the 20-bit processor address-space limit")
        entry = _linear(self.entry_segment, self.entry_offset)
        if not self.load_address <= entry < image_end:
            raise ValueError("entry point is outside the workload image")
        _linear(self.stack_segment, self.stack_offset)
        if self.shared_size:
            if not 0 <= self.shared_base < PROCESSOR_ADDRESS_SPACE_SIZE:
                raise ValueError("shared-memory base is outside the 20-bit processor address space")
            if self.shared_base + self.shared_size > PROCESSOR_ADDRESS_SPACE_SIZE:
                raise ValueError("shared-memory range crosses the 20-bit processor address-space limit")
            if not self.flags & FLAG_SHARED_MEMORY:
                raise ValueError("shared-memory range requires FLAG_SHARED_MEMORY")
        elif self.shared_base or self.flags & FLAG_SHARED_MEMORY:
            raise ValueError("shared-memory flag, base, and size are inconsistent")
        known_flags = (
            FLAG_PERSISTENT | FLAG_STDIO | FLAG_SHARED_MEMORY |
            FLAG_CLOCK_FREE_RUNNING | FLAG_CLOCK_STEPPED
        )
        if self.flags & ~known_flags:
            raise ValueError("workload uses unknown flags")
        if self.flags & FLAG_CLOCK_FREE_RUNNING and self.flags & FLAG_CLOCK_STEPPED:
            raise ValueError("workload requests two execution clock modes")
        return self

    def encode(self) -> bytes:
        self.validated()
        return _MANIFEST.pack(
            MAGIC,
            FORMAT_VERSION,
            MANIFEST_SIZE,
            self.image_size,
            self.image_crc32,
            self.load_address,
            self.entry_segment,
            self.entry_offset,
            self.stack_segment,
            self.stack_offset,
            self.shared_base,
            self.shared_size,
            self.flags,
        )

    @classmethod
    def decode(cls, encoded: bytes) -> "WorkloadManifest":
        if len(encoded) != MANIFEST_SIZE:
            raise ValueError("workload manifest has the wrong size")
        fields = _MANIFEST.unpack(encoded)
        magic, version, header_size = fields[:3]
        if magic != MAGIC or version != FORMAT_VERSION or header_size != MANIFEST_SIZE:
            raise ValueError("unsupported workload manifest")
        return cls(*fields[3:]).validated()


def encode_workload_file(manifest: WorkloadManifest, image: bytes) -> bytes:
    manifest.validated()
    if len(image) != manifest.image_size or zlib.crc32(image) != manifest.image_crc32:
        raise ValueError("image does not match its workload manifest")
    return manifest.encode() + image


def decode_workload_file(encoded: bytes) -> tuple[WorkloadManifest, bytes]:
    if len(encoded) < MANIFEST_SIZE:
        raise ValueError("workload file is truncated")
    manifest = WorkloadManifest.decode(encoded[:MANIFEST_SIZE])
    image = encoded[MANIFEST_SIZE:]
    if len(image) != manifest.image_size or zlib.crc32(image) != manifest.image_crc32:
        raise ValueError("workload image size or CRC is invalid")
    return manifest, image


def upload_records(
    manifest: WorkloadManifest,
    image: bytes,
    *,
    transfer_id: int,
    first_sequence: int = 1,
) -> list[Message]:
    encode_workload_file(manifest, image)
    if not 0 <= transfer_id <= 0xFFFFFFFF:
        raise ValueError("transfer id is not a 32-bit value")
    records = [Message(
        TYPE_WORKLOAD_BEGIN,
        first_sequence,
        _BEGIN_PREFIX.pack(transfer_id) + manifest.encode(),
    )]
    sequence = first_sequence + 1
    for offset in range(0, len(image), DATA_BYTES):
        records.append(Message(
            TYPE_WORKLOAD_DATA,
            sequence,
            _DATA_PREFIX.pack(transfer_id, offset) + image[offset:offset + DATA_BYTES],
        ))
        sequence += 1
    records.append(Message(
        TYPE_WORKLOAD_COMMIT,
        sequence,
        _COMMIT.pack(transfer_id, manifest.image_crc32),
    ))
    return records


def decode_data_payload(payload: bytes) -> tuple[int, int, bytes]:
    if len(payload) < _DATA_PREFIX.size:
        raise ValueError("workload data payload is truncated")
    transfer_id, offset = _DATA_PREFIX.unpack_from(payload)
    return transfer_id, offset, payload[_DATA_PREFIX.size:]


def parse_number(value: str) -> int:
    """Parse the shell's decimal or 0x-prefixed numeric form."""
    try:
        return int(value, 0)
    except ValueError as exc:
        raise ValueError(f"invalid number: {value!r}") from exc


def parse_far_pointer(value: str) -> tuple[int, int]:
    try:
        segment_text, offset_text = value.split(":", 1)
    except ValueError as exc:
        raise ValueError(f"expected segment:offset, got {value!r}") from exc
    segment = int(segment_text, 16)
    offset = int(offset_text, 16)
    _linear(segment, offset)
    return segment, offset


def workload_from_command(
    arguments: tuple[str, ...], *, transfer_id: int, first_sequence: int
) -> tuple[WorkloadManifest, bytes, list[Message]]:
    """Create an upload transaction from one ``load`` shell command."""
    if not arguments:
        raise ValueError(
            "usage: load <bin> [--address N] [--entry CS:IP] "
            "[--stack SS:SP] [--clock auto|free-running|clock-stepped]"
        )

    source = Path(arguments[0])
    load_address = 0x10000
    entry: tuple[int, int] | None = None
    stack = (0, 0)
    clock = "auto"
    index = 1
    while index < len(arguments):
        option = arguments[index]
        if option not in ("--address", "--entry", "--stack", "--clock") or index + 1 >= len(arguments):
            raise ValueError(f"unknown or incomplete load option: {option!r}")
        value = arguments[index + 1]
        if option == "--address":
            load_address = parse_number(value)
        elif option == "--entry":
            entry = parse_far_pointer(value)
        elif option == "--stack":
            stack = parse_far_pointer(value)
        else:
            if value not in ("auto", "free-running", "clock-stepped"):
                raise ValueError(
                    "--clock must be auto, free-running, or clock-stepped"
                )
            clock = value
        index += 2

    try:
        encoded = source.read_bytes()
    except OSError as exc:
        raise ValueError(f"cannot read workload {source}: {exc}") from exc
    if source.suffix.lower() == ".p86w":
        manifest, image = decode_workload_file(encoded)
    else:
        image = encoded
        if entry is None:
            entry = (load_address >> 4, load_address & 0x0F)
        clock_flags = {
            "auto": 0,
            "free-running": FLAG_CLOCK_FREE_RUNNING,
            "clock-stepped": FLAG_CLOCK_STEPPED,
        }[clock]
        manifest = WorkloadManifest.for_image(
            image,
            load_address=load_address,
            entry_segment=entry[0],
            entry_offset=entry[1],
            stack_segment=stack[0],
            stack_offset=stack[1],
            flags=FLAG_STDIO | clock_flags,
        )
    return manifest, image, upload_records(
        manifest, image, transfer_id=transfer_id, first_sequence=first_sequence
    )


def control_record(operation: str, *, workload_id: int, sequence: int) -> Message:
    try:
        operation_id = CONTROL_OPERATIONS[operation]
    except KeyError as exc:
        raise ValueError(f"unknown workload operation: {operation!r}") from exc
    if not 0 <= workload_id <= 0xFFFFFFFF:
        raise ValueError("workload id is not a 32-bit value")
    return Message(
        TYPE_WORKLOAD_CONTROL,
        sequence,
        _CONTROL.pack(operation_id, workload_id),
    )


def decode_status_payload(payload: bytes) -> tuple[int, int, int, int, int, int]:
    """Decode status while accepting the earlier 12- and 20-byte firmware."""
    if len(payload) == _STATUS_V1.size:
        workload_id, state, detail = _STATUS_V1.unpack(payload)
        return workload_id, state, detail, CLOCK_MODES["auto"], 0, 0
    if len(payload) == _STATUS_V2.size:
        return (*_STATUS_V2.unpack(payload), 0)
    if len(payload) != _STATUS.size:
        raise ValueError("workload status payload has the wrong size")
    return _STATUS.unpack(payload)
