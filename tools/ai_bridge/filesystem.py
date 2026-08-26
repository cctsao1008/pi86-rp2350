"""Host-side records for the RP-FLASH 64-byte filesystem service."""

from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

from protocol import (
    FILESYSTEM_DF,
    FILESYSTEM_FLAG_DIRECTORY,
    FILESYSTEM_FLAG_EOF,
    FILESYSTEM_LIST,
    FILESYSTEM_READ,
    FILESYSTEM_READ_PATH_BYTES,
    FILESYSTEM_WRITE_BEGIN,
    FILESYSTEM_WRITE_COMMIT,
    FILESYSTEM_WRITE_DATA,
    FILESYSTEM_WRITE_DATA_BYTES,
    FILESYSTEM_WRITE_PATH_BYTES,
    Message,
    STATUS_BAD_CRC,
    STATUS_BAD_LENGTH,
    STATUS_BAD_STATE,
    STATUS_INVALID_PATH,
    STATUS_IO_ERROR,
    STATUS_NOT_FOUND,
    STATUS_NO_SPACE,
    STATUS_OK,
    STATUS_SERVICE_UNAVAILABLE,
    TYPE_FILESYSTEM_REQUEST,
    TYPE_FILESYSTEM_RESULT,
)

STATUS_NAMES = {
    STATUS_BAD_LENGTH: "bad request length",
    STATUS_SERVICE_UNAVAILABLE: "RP-FLASH service unavailable",
    STATUS_BAD_CRC: "upload CRC mismatch",
    STATUS_BAD_STATE: "filesystem transfer state mismatch",
    STATUS_IO_ERROR: "RP-FLASH I/O error",
    STATUS_NOT_FOUND: "path not found",
    STATUS_INVALID_PATH: "invalid flash: path",
    STATUS_NO_SPACE: "no space or target denied",
}


@dataclass(frozen=True)
class ListEntry:
    name: str
    size: int
    directory: bool
    next_cursor: int
    eof: bool = False


@dataclass(frozen=True)
class DiskFree:
    label: str
    filesystem_type: int
    total_kib: int
    free_kib: int
    cluster_bytes: int
    erase_bytes: int


@dataclass(frozen=True)
class ReadChunk:
    offset: int
    total_size: int
    data: bytes
    eof: bool


def _path(path: str, maximum: int) -> bytes:
    encoded = path.encode("utf-8")
    if not path.startswith("flash:") or (len(path) > 6 and path[6] != "/"):
        raise ValueError("path must use flash: or flash:/...")
    if not encoded or len(encoded) > maximum:
        raise ValueError(f"flash: path exceeds {maximum} UTF-8 bytes")
    return encoded


def request(operation: int, sequence: int, payload: bytes) -> Message:
    return Message(TYPE_FILESYSTEM_REQUEST, sequence, bytes([operation]) + payload)


def list_request(path: str, cursor: int, sequence: int) -> Message:
    encoded = _path(path, 48)
    return request(FILESYSTEM_LIST, sequence,
                   bytes([len(encoded)]) + struct.pack("<H", cursor) + encoded)


def df_request(path: str, sequence: int) -> Message:
    encoded = _path(path, 50)
    return request(FILESYSTEM_DF, sequence, bytes([len(encoded)]) + encoded)


def read_request(path: str, offset: int, sequence: int) -> Message:
    encoded = _path(path, FILESYSTEM_READ_PATH_BYTES)
    return request(FILESYSTEM_READ, sequence,
                   bytes([len(encoded), 0, 0]) + struct.pack("<I", offset) + encoded)


def write_records(path: str, data: bytes, transfer_id: int,
                  first_sequence: int) -> list[Message]:
    encoded = _path(path, FILESYSTEM_WRITE_PATH_BYTES)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    sequence = first_sequence
    begin = request(
        FILESYSTEM_WRITE_BEGIN,
        sequence,
        bytes([len(encoded), 0, 0]) +
        struct.pack("<III", transfer_id, len(data), crc) + encoded,
    )
    records = [begin]
    offset = 0
    while offset < len(data):
        chunk = data[offset:offset + FILESYSTEM_WRITE_DATA_BYTES]
        sequence = (sequence + 1) & 0xFFFFFFFF
        records.append(request(
            FILESYSTEM_WRITE_DATA,
            sequence,
            b"\0" + struct.pack("<HII", len(chunk), transfer_id, offset) + chunk,
        ))
        offset += len(chunk)
    sequence = (sequence + 1) & 0xFFFFFFFF
    records.append(request(
        FILESYSTEM_WRITE_COMMIT,
        sequence,
        b"\0\0\0" + struct.pack("<I", transfer_id),
    ))
    return records


def validate_reply(reply: Message, request_record: Message) -> bytes:
    if reply.message_type != TYPE_FILESYSTEM_RESULT:
        raise ValueError(f"unexpected filesystem reply type: {reply.message_type}")
    if reply.sequence != request_record.sequence:
        raise ValueError(
            f"filesystem reply sequence mismatch: {reply.sequence} != "
            f"{request_record.sequence}"
        )
    if reply.status != STATUS_OK:
        raise ValueError(STATUS_NAMES.get(
            reply.status, f"filesystem request failed with status {reply.status}"
        ))
    if not reply.payload or reply.payload[0] != request_record.payload[0]:
        raise ValueError("filesystem operation echo mismatch")
    return reply.payload


def parse_list(reply: Message, request_record: Message) -> ListEntry:
    payload = validate_reply(reply, request_record)
    if len(payload) < 10:
        raise ValueError("filesystem list reply is truncated")
    flags, name_length = payload[1], payload[3]
    if name_length > len(payload) - 10:
        raise ValueError("filesystem list name is truncated")
    return ListEntry(
        name=payload[10:10 + name_length].decode("utf-8", errors="replace"),
        size=struct.unpack_from("<I", payload, 6)[0],
        directory=bool(flags & FILESYSTEM_FLAG_DIRECTORY),
        next_cursor=struct.unpack_from("<H", payload, 4)[0],
        eof=bool(flags & FILESYSTEM_FLAG_EOF),
    )


def parse_df(reply: Message, request_record: Message) -> DiskFree:
    payload = validate_reply(reply, request_record)
    if len(payload) < 20:
        raise ValueError("filesystem df reply is truncated")
    label_length = payload[2]
    if label_length > len(payload) - 20:
        raise ValueError("filesystem label is truncated")
    total, free, cluster, erase = struct.unpack_from("<IIII", payload, 4)
    return DiskFree(
        label=payload[20:20 + label_length].decode("ascii", errors="replace"),
        filesystem_type=payload[1], total_kib=total, free_kib=free,
        cluster_bytes=cluster, erase_bytes=erase,
    )


def parse_read(reply: Message, request_record: Message) -> ReadChunk:
    payload = validate_reply(reply, request_record)
    if len(payload) < 12:
        raise ValueError("filesystem read reply is truncated")
    data_length = struct.unpack_from("<H", payload, 2)[0]
    if data_length > len(payload) - 12:
        raise ValueError("filesystem read data is truncated")
    offset, total = struct.unpack_from("<II", payload, 4)
    return ReadChunk(offset, total, payload[12:12 + data_length],
                     bool(payload[1] & FILESYSTEM_FLAG_EOF))
