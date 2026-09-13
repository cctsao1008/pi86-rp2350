from __future__ import annotations

import unittest
from pathlib import Path

from tools.ia16_lab.omf import contribution_bases, parse_omf_bytes


def record(record_type: int, payload: bytes) -> bytes:
    # The parser does not validate checksums; keep a placeholder byte so the
    # record length follows the OMF convention used by real Open Watcom files.
    body = payload + b"\x00"
    return bytes((record_type,)) + len(body).to_bytes(2, "little") + body


def name(value: str) -> bytes:
    encoded = value.encode("ascii")
    return bytes((len(encoded),)) + encoded


def segdef(length: int, name_index: int, class_index: int, *, alignment_code: int) -> bytes:
    attributes = alignment_code << 5
    return bytes((attributes,)) + length.to_bytes(2, "little") + bytes(
        (name_index, class_index, 1)
    )


def pubdef(
    record_type: int,
    segment_index: int,
    symbols: tuple[tuple[str, int], ...],
) -> bytes:
    payload = bytearray((0, segment_index))
    for symbol_name, offset in symbols:
        payload.extend(name(symbol_name))
        payload.extend(offset.to_bytes(2, "little"))
        payload.append(0)
    return record(record_type, bytes(payload))


def sample_object(*, text_length: int, data_length: int, local_offset: int, path: str):
    data = b"".join(
        (
            record(0x96, b"".join((name(""), name("_TEXT"), name("CODE"), name("_BSS"), name("BSS")))),
            record(0x98, segdef(text_length, 2, 3, alignment_code=1)),
            record(0x98, segdef(data_length, 4, 5, alignment_code=2)),
            pubdef(0x90, 1, (("_publicAnchor", 0x10),)),
            pubdef(0xB6, 1, (("_localFunction", local_offset),)),
            pubdef(0xB6, 2, (("_localObject", 0x28),)),
        )
    )
    return parse_omf_bytes(data, path=Path(path))


class OMFTests(unittest.TestCase):
    def test_parses_public_and_local_public_symbols(self):
        obj = sample_object(text_length=0x100, data_length=0x40, local_offset=0x55, path="a.obj")

        self.assertEqual(obj.segment("_TEXT").length, 0x100)
        self.assertEqual(obj.segment("_BSS").alignment, 2)
        self.assertEqual(obj.symbol("_publicAnchor").offset, 0x10)
        self.assertFalse(obj.symbol("_publicAnchor").local)
        self.assertEqual(obj.symbol("_localFunction").segment_name, "_TEXT")
        self.assertEqual(obj.symbol("_localFunction").offset, 0x55)
        self.assertTrue(obj.symbol("_localFunction").local)
        self.assertEqual(obj.symbol("_localObject").segment_name, "_BSS")

    def test_contribution_bases_follow_link_order_and_alignment(self):
        first = sample_object(text_length=3, data_length=3, local_offset=0, path="first.obj")
        second = sample_object(text_length=5, data_length=4, local_offset=0, path="second.obj")

        bases = contribution_bases((first, second))

        self.assertEqual(bases[(Path("first.obj"), "_TEXT")], 0)
        self.assertEqual(bases[(Path("second.obj"), "_TEXT")], 3)
        self.assertEqual(bases[(Path("first.obj"), "_BSS")], 0)
        self.assertEqual(bases[(Path("second.obj"), "_BSS")], 4)


if __name__ == "__main__":
    unittest.main()
