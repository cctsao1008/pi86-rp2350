"""High-level memory and filesystem services over the RP86 request channel."""

from __future__ import annotations

from collections.abc import Callable

from .filesystem import (
    ListEntry,
    list_request,
    parse_list,
    parse_read,
    read_request,
    validate_reply as validate_filesystem_payload,
)
from .memory import (
    memory_read_request,
    memory_write_records,
    parse_memory_read,
    validate_memory_reply,
)
from .protocol import Message
from .request_channel import RequestExchange
from .runtime_state import RequestSequence

class RuntimeServiceClient:
    """Sequence-safe access to RP2350 memory and filesystem services."""

    def __init__(
        self,
        exchange: RequestExchange,
        sequence: RequestSequence,
        on_activity: Callable[[], None],
    ) -> None:
        self._exchange = exchange
        self._sequence = sequence
        self._on_activity = on_activity

    def filesystem_request(
        self, request: Message
    ) -> tuple[Message | None, str | None]:
        reply, _latency_ms, error = self._exchange(request)
        self._sequence.advance_after(request.sequence)
        self._on_activity()
        if reply is None:
            return None, error or "filesystem exchange failed"
        try:
            validate_filesystem_payload(reply, request)
        except ValueError as exc:
            return None, str(exc)
        return reply, None

    def memory_request(
        self, request: Message
    ) -> tuple[Message | None, str | None]:
        reply, _latency_ms, error = self._exchange(request)
        self._sequence.advance_after(request.sequence)
        self._on_activity()
        if reply is None:
            return None, error or "memory exchange failed"
        try:
            validate_memory_reply(reply, request)
        except ValueError as exc:
            return None, str(exc)
        return reply, None

    def read_memory(
        self, address: int, length: int
    ) -> tuple[bytes | None, str | None]:
        content = bytearray()
        while len(content) < length:
            chunk_length = min(40, length - len(content))
            try:
                request = memory_read_request(
                    address + len(content), chunk_length, self._sequence.value
                )
            except ValueError as exc:
                return None, str(exc)
            reply, error = self.memory_request(request)
            if reply is None:
                return None, error
            try:
                content.extend(parse_memory_read(reply, request))
            except ValueError as exc:
                return None, str(exc)
        return bytes(content), None

    def write_memory(self, address: int, data: bytes) -> str | None:
        try:
            records = memory_write_records(
                address, data, self._sequence.value
            )
        except ValueError as exc:
            return str(exc)
        for request in records:
            reply, error = self.memory_request(request)
            if reply is None:
                return error
        return None

    def read_directory(
        self, path: str
    ) -> tuple[list[ListEntry] | None, str | None]:
        cursor = 0
        entries: list[ListEntry] = []
        while True:
            try:
                request = list_request(path, cursor, self._sequence.value)
            except ValueError as exc:
                return None, str(exc)
            reply, error = self.filesystem_request(request)
            if reply is None:
                return None, error
            try:
                entry = parse_list(reply, request)
            except ValueError as exc:
                return None, f"invalid device reply: {exc}"
            if entry.eof:
                return entries, None
            entries.append(entry)
            cursor = entry.next_cursor

    def read_file(self, path: str) -> tuple[bytes | None, str | None]:
        offset = 0
        total_size: int | None = None
        content = bytearray()
        while True:
            try:
                request = read_request(path, offset, self._sequence.value)
            except ValueError as exc:
                return None, str(exc)
            reply, error = self.filesystem_request(request)
            if reply is None:
                return None, error
            try:
                chunk = parse_read(reply, request)
            except ValueError as exc:
                return None, f"invalid device reply: {exc}"
            if chunk.offset != offset:
                return None, f"reply offset mismatch {chunk.offset} != {offset}"
            if total_size is None:
                total_size = chunk.total_size
            elif chunk.total_size != total_size:
                return None, "file size changed while reading"
            content.extend(chunk.data)
            offset += len(chunk.data)
            if chunk.eof:
                if total_size != len(content):
                    return None, (
                        f"file length mismatch {len(content)} != {total_size}"
                    )
                return bytes(content), None
            if not chunk.data:
                return None, "device returned an empty non-final file chunk"
