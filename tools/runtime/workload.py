"""Flat native V30 workload format and fixed-record upload transport."""

from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

from protocol import (
    Message,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_COMMIT,
    TYPE_WORKLOAD_DATA,
)

MAGIC = 0x57363850  # "P86W" in little endian
FORMAT_VERSION = 1
V30_ADDRESS_SPACE_SIZE = 0x100000

FLAG_PERSISTENT = 1 << 0
FLAG_STDIO = 1 << 1
FLAG_SHARED_MEMORY = 1 << 2

_MANIFEST = struct.Struct("<IHHIIIHHHHIII")
_BEGIN_PREFIX = struct.Struct("<I")
_DATA_PREFIX = struct.Struct("<II")
_COMMIT = struct.Struct("<II")

MANIFEST_SIZE = _MANIFEST.size
HOST_PAYLOAD_SIZE = 52
DATA_BYTES = HOST_PAYLOAD_SIZE - _DATA_PREFIX.size


def _linear(segment: int, offset: int) -> int:
    if not 0 <= segment <= 0xFFFF or not 0 <= offset <= 0xFFFF:
        raise ValueError("segment and offset must be 16-bit values")
    address = (segment << 4) + offset
    if address >= V30_ADDRESS_SPACE_SIZE:
        raise ValueError("segment:offset is outside the V30 address space")
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
        if not 0 <= self.load_address < V30_ADDRESS_SPACE_SIZE:
            raise ValueError("load address is outside the V30 address space")
        image_end = self.load_address + self.image_size
        if image_end > V30_ADDRESS_SPACE_SIZE:
            raise ValueError("workload image crosses the V30 address-space limit")
        entry = _linear(self.entry_segment, self.entry_offset)
        if not self.load_address <= entry < image_end:
            raise ValueError("entry point is outside the workload image")
        _linear(self.stack_segment, self.stack_offset)
        if self.shared_size:
            if not 0 <= self.shared_base < V30_ADDRESS_SPACE_SIZE:
                raise ValueError("shared-memory base is outside the V30 address space")
            if self.shared_base + self.shared_size > V30_ADDRESS_SPACE_SIZE:
                raise ValueError("shared-memory range crosses the V30 address-space limit")
            if not self.flags & FLAG_SHARED_MEMORY:
                raise ValueError("shared-memory range requires FLAG_SHARED_MEMORY")
        elif self.shared_base or self.flags & FLAG_SHARED_MEMORY:
            raise ValueError("shared-memory flag, base, and size are inconsistent")
        if self.flags & ~(FLAG_PERSISTENT | FLAG_STDIO | FLAG_SHARED_MEMORY):
            raise ValueError("workload uses unknown flags")
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
