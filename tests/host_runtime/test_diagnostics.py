from dataclasses import replace
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from rp86_runtime.core import validate_device_reply
from rp86_runtime.console import CdcDisplayStream
from rp86_runtime.diagnostics import BusDiagnostics, diagnostics_request
from rp86_runtime.protocol import Message, TYPE_DIAGNOSTICS_RESULT, STATUS_BAD_STATE
from rp86_runtime.runtime_state import RequestSequence
from rp86_runtime.service_client import RuntimeServiceClient
from rp86_runtime.session_evidence import SessionEvidence


class DiagnosticsTests(unittest.TestCase):
    def test_cdc_fault_events_can_trigger_structured_refresh(self):
        stream = CdcDisplayStream()
        self.assertEqual(stream.feed(b"[WORKLOAD FAULT] cyc"), ())
        self.assertEqual(stream.feed(b"le=9\r\n[WORKLOAD TIMEOUT] no ALE\n"),
                         ("[WORKLOAD FAULT] cycle=9", "[WORKLOAD TIMEOUT] no ALE"))

    def setUp(self):
        self.request = diagnostics_request(7, 19)
        self.reply = Message(TYPE_DIAGNOSTICS_RESULT, 19, struct.pack(
            "<13I", 7, 3, 6, 4, 9, 0x40000, 0, 0, 3, 9, 0, 0, 0))

    def test_fixed_records_and_last_cycle(self):
        self.assertEqual(len(self.request.encode()), 64)
        self.assertEqual(len(self.reply.encode()), 64)
        snapshot = BusDiagnostics.from_reply(self.reply, self.request)
        self.assertEqual(snapshot.as_dict()["fault_flags"], ["UNMAPPED"])
        self.assertEqual(snapshot.as_dict()["last_address"], 0x40000)
        self.assertIsNone(snapshot.as_dict()["last_data"])
        self.assertIn("MEM_READ address=0x40000", snapshot.format())
        self.assertIn("data=unavailable", snapshot.format())

    def test_matching_rejection_is_not_stale(self):
        reply = replace(self.reply, status=STATUS_BAD_STATE, payload=b"")
        self.assertEqual(validate_device_reply(reply.encode(), self.request), reply)
        with self.assertRaisesRegex(ValueError, "processor is executing"):
            BusDiagnostics.from_reply(reply, self.request)

    def test_reject_mismatch_truncation_and_reserved_data(self):
        invalid = [replace(self.reply, sequence=20), replace(self.reply, message_type=5),
                   replace(self.reply, payload=self.reply.payload[:-4]),
                   replace(self.reply, payload=self.reply.payload[:-4] + b"\1\0\0\0"),
                   replace(self.reply, payload=struct.pack("<I", 8) + self.reply.payload[4:])]
        for reply in invalid:
            with self.subTest(reply=reply), self.assertRaises(ValueError):
                BusDiagnostics.from_reply(reply, self.request)

    def test_absent_cycle_is_not_zero_address(self):
        reply = replace(self.reply, payload=struct.pack("<13I", 7, 0, 6, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0))
        snapshot = BusDiagnostics.from_reply(reply, self.request)
        self.assertIsNone(snapshot.as_dict()["last_address"])
        self.assertIsNone(snapshot.as_dict()["cycle_name"])

    def test_service_advances_sequence_on_rejection(self):
        sequence = RequestSequence(19)
        reply = replace(self.reply, status=STATUS_BAD_STATE, payload=b"")
        client = RuntimeServiceClient(lambda request: (reply, 1.0, None), sequence, lambda: None)
        snapshot, error = client.read_diagnostics(7)
        self.assertIsNone(snapshot)
        self.assertIn("stop before trace", error)
        self.assertEqual(sequence.value, 20)

    def test_json_retains_snapshot_without_duplicate_poll_entries(self):
        snapshot = BusDiagnostics.from_reply(self.reply, self.request)
        with tempfile.TemporaryDirectory() as directory:
            evidence = SessionEvidence(Path(directory) / "raw.log", Path(directory) / "session.json")
            evidence.retain_diagnostics(snapshot)
            evidence.retain_diagnostics(snapshot)
            evidence.write({})
            saved = json.loads(evidence.json_path.read_text())["bus_diagnostics"]
            self.assertEqual(len(saved), 1)
            self.assertEqual(saved[0]["workload_id"], 7)
            self.assertEqual(saved[0]["boot_id"], 3)
            self.assertEqual(saved[0]["last_address"], 0x40000)
            self.assertEqual(saved[0]["reason"], "BUS_FAULT")
