from dataclasses import replace
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from rp86_runtime.core import validate_device_reply
from rp86_runtime.protocol import Message, TYPE_WORKLOAD_TIMEOUT_RESULT, STATUS_BAD_STATE
from rp86_runtime.runtime_state import RequestSequence
from rp86_runtime.service_client import RuntimeServiceClient
from rp86_runtime.workload_timeout import WorkloadTimeout, parse_timeout, timeout_request


class WorkloadTimeoutTests(unittest.TestCase):
    def test_parse_query_off_and_millisecond_values(self):
        for value, expected in [((), None), (("off",), 0), (("5",), 5000),
                                (("0.001",), 1), (("86400",), 86400000)]:
            self.assertEqual(parse_timeout(value), expected)
        for value in ("0", "-1", "NaN", "Infinity", "oops", "0.0001", "86401", "1e1000000"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_timeout((value,))
        with self.assertRaises(ValueError):
            parse_timeout(("5", "extra"))

    def test_fixed_record_and_query_result(self):
        request = timeout_request(None, 21)
        reply = Message(TYPE_WORKLOAD_TIMEOUT_RESULT, 21,
                        struct.pack("<13I", 5000, 2000, 7, 3, 1, *([0] * 8)))
        self.assertEqual(len(request.encode()), 64)
        self.assertEqual(len(reply.encode()), 64)
        state = WorkloadTimeout.from_reply(reply, request)
        self.assertEqual(state.remaining_ms, 2000)
        self.assertIn("5.000s", state.format())
        self.assertEqual(validate_device_reply(reply.encode(), request), reply)
        for bad in (replace(reply, sequence=22), replace(reply, payload=b""),
                    replace(reply, payload=reply.payload[:-4] + b"\1\0\0\0")):
            with self.assertRaises(ValueError):
                WorkloadTimeout.from_reply(bad, request)
        with self.assertRaisesRegex(ValueError, "not applied"):
            WorkloadTimeout.from_reply(reply, timeout_request(1000, 21))

    def test_negative_reply_returns_immediately_and_advances_sequence(self):
        reply = Message(TYPE_WORKLOAD_TIMEOUT_RESULT, 21, status=STATUS_BAD_STATE)
        request = timeout_request(1000, 21)
        self.assertEqual(validate_device_reply(reply.encode(), request), reply)
        sequence = RequestSequence(21)
        client = RuntimeServiceClient(lambda request: (reply, 1.0, None), sequence, lambda: None)
        state, error = client.execution_timeout(1000)
        self.assertIsNone(state)
        self.assertIn("prepared workload", error)
        self.assertEqual(sequence.value, 22)
