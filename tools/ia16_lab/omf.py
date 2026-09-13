"""Minimal Intel/Microsoft OMF reader for RP86 IA16 evidence.

The IA16 lab only needs a narrow subset of 16-bit OMF: segment definitions and
public/local-public symbol offsets.  Open Watcom emits static C functions and
objects as LPUBDEF records, which lets the lab recover exact object-relative
locations without changing FreeRTOS linkage or relying on disassembler output.

This is intentionally not a general OMF implementation.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


THEADR = 0x80
PUBDEF = 0x90
PUBDEF32 = 0x91
LNAMES = 0x96
SEGDEF = 0x98
SEGDEF32 = 0x99
LPUBDEF = 0xB6
LPUBDEF32 = 0xB7


@dataclass(frozen=True)
class OMFSegment:
    index: int
    name: str
    class_name: str
    length: int
    alignment: int


@dataclass(frozen=True)
class OMFSymbol:
    name: str
    segment_index: int
    segment_name: str
    offset: int
    local: bool


@dataclass(frozen=True)
class OMFObject:
    path: Path | None
    segments: tuple[OMFSegment, ...]
    symbols: tuple[OMFSymbol, ...]

    def segment(self, name: str) -> OMFSegment | None:
        return next((segment for segment in self.segments if segment.name == name), None)

    def symbol(self, name: str) -> OMFSymbol | None:
        return next((symbol for symbol in self.symbols if symbol.name == name), None)


def _index(payload: bytes, position: int) -> tuple[int, int]:
    first = payload[position]
    if first & 0x80:
        return ((first & 0x7F) << 8) | payload[position + 1], position + 2
    return first, position + 1


def _records(data: bytes):
    position = 0
    while position < len(data):
        if position + 3 > len(data):
            raise ValueError("truncated OMF record header")
        record_type = data[position]
        length = int.from_bytes(data[position + 1 : position + 3], "little")
        end = position + 3 + length
        if end > len(data):
            raise ValueError("truncated OMF record payload")
        # OMF length includes the one-byte checksum.  Consumers below only need
        # record contents, so strip it here.
        yield record_type, data[position + 3 : end - 1]
        position = end


def _alignment_bytes(code: int) -> int:
    # Intel OMF SEGDEF A-field values used by the RP86 toolchain.
    # 0 is absolute and therefore has no concatenation alignment.
    return {
        0: 1,
        1: 1,   # byte
        2: 2,   # word
        3: 16,  # paragraph
        4: 256, # page
        5: 4,   # dword
    }.get(code, 1)


def parse_omf_bytes(data: bytes, *, path: Path | None = None) -> OMFObject:
    names: list[str | None] = [None]
    segments: list[OMFSegment | None] = [None]
    symbols: list[OMFSymbol] = []

    for record_type, payload in _records(data):
        if record_type == LNAMES:
            position = 0
            while position < len(payload):
                size = payload[position]
                position += 1
                names.append(payload[position : position + size].decode("latin1"))
                position += size
            continue

        if record_type in (SEGDEF, SEGDEF32):
            position = 0
            attributes = payload[position]
            position += 1
            alignment_code = (attributes >> 5) & 0x07
            if alignment_code == 0:
                # Absolute segment: frame (2 bytes) + offset (1 byte).
                position += 3
            width = 4 if record_type == SEGDEF32 else 2
            length = int.from_bytes(payload[position : position + width], "little")
            position += width
            name_index, position = _index(payload, position)
            class_index, position = _index(payload, position)
            _, position = _index(payload, position)  # overlay name
            segment = OMFSegment(
                index=len(segments),
                name=names[name_index] or "",
                class_name=names[class_index] or "",
                length=length,
                alignment=_alignment_bytes(alignment_code),
            )
            segments.append(segment)
            continue

        if record_type not in (PUBDEF, PUBDEF32, LPUBDEF, LPUBDEF32):
            continue

        position = 0
        _, position = _index(payload, position)  # group index
        segment_index, position = _index(payload, position)
        if segment_index == 0:
            # Absolute PUBDEF includes a frame word.  It is irrelevant to the
            # relocatable RP86 objects, but skip it correctly.
            position += 2
        width = 4 if record_type in (PUBDEF32, LPUBDEF32) else 2
        local = record_type in (LPUBDEF, LPUBDEF32)
        while position < len(payload):
            size = payload[position]
            position += 1
            name = payload[position : position + size].decode("latin1")
            position += size
            offset = int.from_bytes(payload[position : position + width], "little")
            position += width
            _, position = _index(payload, position)  # type index
            segment_name = ""
            if segment_index and segment_index < len(segments):
                segment = segments[segment_index]
                if segment is not None:
                    segment_name = segment.name
            symbols.append(
                OMFSymbol(
                    name=name,
                    segment_index=segment_index,
                    segment_name=segment_name,
                    offset=offset,
                    local=local,
                )
            )

    return OMFObject(
        path=path,
        segments=tuple(segment for segment in segments[1:] if segment is not None),
        symbols=tuple(symbols),
    )


def parse_omf(path: Path) -> OMFObject:
    return parse_omf_bytes(path.read_bytes(), path=path)


def contribution_bases(objects: tuple[OMFObject, ...]) -> dict[tuple[Path | None, str], int]:
    """Return each object's offset inside each concatenated named segment.

    WLINK concatenates same-named segment contributions in object-link order and
    honors each contribution's SEGDEF alignment.  The caller should verify at
    least one resulting public address against the final linker map before using
    local-symbol addresses as evidence.
    """

    cursors: dict[tuple[str, str], int] = {}
    bases: dict[tuple[Path | None, str], int] = {}
    for obj in objects:
        for segment in obj.segments:
            key = (segment.name, segment.class_name)
            cursor = cursors.get(key, 0)
            alignment = max(segment.alignment, 1)
            cursor = (cursor + alignment - 1) // alignment * alignment
            bases[(obj.path, segment.name)] = cursor
            cursors[key] = cursor + segment.length
    return bases
