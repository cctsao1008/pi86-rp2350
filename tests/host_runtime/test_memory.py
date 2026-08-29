from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.mailbox import (  # noqa: E402
    MAILBOX_BASE,
    MAILBOX_MAGIC,
    OWNER_PROCESSOR,
    STATUS_REQUEST_READY,
    MailboxHeader,
    mailbox_commit_records,
)
from rp86_runtime.memory import (  # noqa: E402
    format_memory_dump,
    memory_read_request,
    memory_write_records,
    parse_memory_read,
)
from rp86_runtime.protocol import (  # noqa: E402
    MEMORY_READ,
    Message,
    TYPE_MEMORY_RESULT,
)


class HostMemoryTests(unittest.TestCase):
    def test_chunked_write_and_sequence(self) -> None:
        records = memory_write_records(0x1000, bytes(range(100)), 9)
        self.assertEqual([record.sequence for record in records], [9, 10, 11])
        self.assertEqual([len(record.payload) - 12 for record in records], [40, 40, 20])

    def test_read_reply_is_address_and_length_bound(self) -> None:
        request = memory_read_request(0x1234, 3, 8)
        reply = Message(TYPE_MEMORY_RESULT, 8, request.payload + b"abc")
        self.assertEqual(parse_memory_read(reply, request), b"abc")
        self.assertEqual(request.payload[0], MEMORY_READ)

    def test_mailbox_commit_transfers_owner_last(self) -> None:
        records = mailbox_commit_records(b"hello", 7, 20)
        final = records[-1]
        self.assertEqual(final.payload[4:8], (MAILBOX_BASE + 8).to_bytes(4, "little"))
        self.assertEqual(final.payload[12:], OWNER_PROCESSOR.to_bytes(2, "little"))
        header_bytes = b"".join(
            record.payload[12:] for record in records
            if int.from_bytes(record.payload[4:8], "little") < MAILBOX_BASE + 32
            and record is not final
        )
        self.assertIn(MAILBOX_MAGIC, header_bytes)
        self.assertIn(STATUS_REQUEST_READY.to_bytes(2, "little"), header_bytes)

    def test_dump_uses_physical_addresses(self) -> None:
        rendered = format_memory_dump(0x3F000, b"RP86\0")
        self.assertIn("3F000", rendered)
        self.assertIn("52 50 38 36 00", rendered)

    def test_mailbox_header_decodes_canonical_layout(self) -> None:
        records = mailbox_commit_records(b"hello", 7, 20)
        image = bytearray(32)
        for record in records[:-1]:
            address = int.from_bytes(record.payload[4:8], "little")
            if MAILBOX_BASE <= address < MAILBOX_BASE + 32:
                data = record.payload[12:]
                image[address - MAILBOX_BASE:address - MAILBOX_BASE + len(data)] = data
        header = MailboxHeader.decode(bytes(image))
        self.assertEqual(header.generation, 7)
        self.assertEqual(header.request_length, 5)


if __name__ == "__main__":
    unittest.main()
