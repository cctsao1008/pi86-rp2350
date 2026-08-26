import struct
import unittest

from filesystem import (
    df_request,
    list_request,
    parse_df,
    parse_list,
    parse_read,
    read_request,
    validate_reply,
    write_records,
)
from protocol import (
    FILESYSTEM_DF,
    FILESYSTEM_FLAG_DIRECTORY,
    FILESYSTEM_FLAG_EOF,
    FILESYSTEM_LIST,
    FILESYSTEM_READ,
    FILESYSTEM_WRITE_BEGIN,
    FILESYSTEM_WRITE_COMMIT,
    FILESYSTEM_WRITE_DATA,
    Message,
    STATUS_NOT_FOUND,
    TYPE_FILESYSTEM_RESULT,
)


class FilesystemProtocolTest(unittest.TestCase):
    def reply(self, request, payload, status=0):
        return Message(TYPE_FILESYSTEM_RESULT, request.sequence, payload,
                       status=status)

    def test_list_request_and_reply(self):
        request = list_request("flash:/", 7, 10)
        self.assertEqual(request.payload[0], FILESYSTEM_LIST)
        self.assertEqual(struct.unpack_from("<H", request.payload, 2)[0], 7)
        name = b"HELLO.TXT"
        payload = bytearray(52)
        payload[0] = FILESYSTEM_LIST
        payload[1] = FILESYSTEM_FLAG_DIRECTORY
        payload[3] = len(name)
        struct.pack_into("<H", payload, 4, 8)
        struct.pack_into("<I", payload, 6, 123)
        payload[10:10 + len(name)] = name
        entry = parse_list(self.reply(request, bytes(payload)), request)
        self.assertEqual(entry.name, "HELLO.TXT")
        self.assertEqual(entry.size, 123)
        self.assertTrue(entry.directory)
        self.assertEqual(entry.next_cursor, 8)

    def test_list_eof(self):
        request = list_request("flash:/", 0, 1)
        payload = bytearray(52)
        payload[0] = FILESYSTEM_LIST
        payload[1] = FILESYSTEM_FLAG_EOF
        self.assertTrue(parse_list(self.reply(request, bytes(payload)), request).eof)

    def test_df_reply(self):
        request = df_request("flash:", 2)
        self.assertEqual(request.payload[0], FILESYSTEM_DF)
        payload = bytearray(52)
        payload[0:3] = bytes([FILESYSTEM_DF, 2, 8])
        struct.pack_into("<IIII", payload, 4, 12288, 12000, 4096, 4096)
        payload[20:28] = b"RP-FLASH"
        result = parse_df(self.reply(request, bytes(payload)), request)
        self.assertEqual(result.label, "RP-FLASH")
        self.assertEqual(result.total_kib, 12288)
        self.assertEqual(result.free_kib, 12000)

    def test_read_reply(self):
        request = read_request("flash:/hello.txt", 40, 3)
        self.assertEqual(request.payload[0], FILESYSTEM_READ)
        payload = bytearray(17)
        payload[0] = FILESYSTEM_READ
        payload[1] = FILESYSTEM_FLAG_EOF
        struct.pack_into("<HII", payload, 2, 5, 40, 45)
        payload[12:17] = b"hello"
        chunk = parse_read(self.reply(request, bytes(payload)), request)
        self.assertEqual(chunk.data, b"hello")
        self.assertTrue(chunk.eof)

    def test_write_record_shape_and_sequences(self):
        records = write_records("flash:/sample.bin", b"A" * 81,
                                0x12345678, 100)
        self.assertEqual([record.sequence for record in records],
                         [100, 101, 102, 103, 104])
        self.assertEqual(records[0].payload[0], FILESYSTEM_WRITE_BEGIN)
        self.assertEqual(records[1].payload[0], FILESYSTEM_WRITE_DATA)
        self.assertEqual(records[-1].payload[0], FILESYSTEM_WRITE_COMMIT)
        self.assertEqual(len(records[1].payload), 52)

    def test_error_status_is_not_silently_accepted(self):
        request = read_request("flash:/missing", 0, 5)
        reply = self.reply(request, bytes([FILESYSTEM_READ]), STATUS_NOT_FOUND)
        with self.assertRaisesRegex(ValueError, "path not found"):
            validate_reply(reply, request)

    def test_rejects_non_flash_paths(self):
        with self.assertRaisesRegex(ValueError, "flash"):
            df_request("sd:", 1)


if __name__ == "__main__":
    unittest.main()
