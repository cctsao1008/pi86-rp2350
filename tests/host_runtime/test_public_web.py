from pathlib import Path
import base64
import struct
import sys
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import rp86_public  # noqa: E402
from rp86_runtime.protocol import (  # noqa: E402
    Message,
    TYPE_WORKLOAD_STATUS,
)
from rp86_web_api import WebApi  # noqa: E402


class PublicSessionTests(unittest.TestCase):
    def test_exclusive_acquire_resume_and_release(self) -> None:
        session = rp86_public.PublicSession()

        first, status = session.acquire(owner="browser-a")
        self.assertEqual(status, 200)
        self.assertTrue(first["ok"])
        token = str(first["token"])
        self.assertTrue(session.authorize(token))

        busy, status = session.acquire(owner="browser-b")
        self.assertEqual(status, 409)
        self.assertFalse(busy["ok"])

        resumed, status = session.acquire(token=token, owner="browser-a")
        self.assertEqual(status, 200)
        self.assertTrue(resumed["ok"])
        self.assertTrue(resumed["resumed"])
        self.assertEqual(resumed["token"], token)

        denied, status = session.release("wrong-token")
        self.assertEqual(status, 403)
        self.assertFalse(denied["ok"])

        released, status = session.release(token)
        self.assertEqual(status, 200)
        self.assertTrue(released["released"])
        self.assertFalse(session.snapshot()["owned"])

    def test_snapshot_reports_requester_ownership(self) -> None:
        session = rp86_public.PublicSession()
        acquired, _ = session.acquire(owner="browser-a")
        token = str(acquired["token"])

        self.assertTrue(session.snapshot(token)["mine"])
        self.assertFalse(session.snapshot("stale-token")["mine"])
        self.assertTrue(session.snapshot("stale-token")["owned"])

    def test_session_has_no_implicit_time_limit(self) -> None:
        session = rp86_public.PublicSession()
        with patch.object(rp86_public.time, "time", return_value=10.0):
            acquired, _ = session.acquire(owner="browser")
        token = str(acquired["token"])
        with patch.object(rp86_public.time, "time", return_value=10_000_000.0):
            self.assertTrue(session.authorize(token))
        self.assertTrue(session.snapshot()["owned"])

    def test_release_is_idempotent_when_available(self) -> None:
        session = rp86_public.PublicSession()
        released, status = session.release(None)
        self.assertEqual(status, 200)
        self.assertTrue(released["ok"])
        self.assertFalse(released["released"])

    def test_public_page_exposes_native_workload_lifecycle(self) -> None:
        html = rp86_public.INDEX_HTML
        for marker in (
            'id="workloadFile"',
            'id="load"',
            'id="run"',
            'id="stop"',
            'id="restart"',
            'id="workloadStatus"',
            "/api/workload",
            "/api/workload/control",
            "FileReader",
        ):
            self.assertIn(marker, html)

    def test_request_body_budget_covers_one_megabyte_processor_image(self) -> None:
        encoded = ((0x100000 + 2) // 3) * 4
        self.assertGreater(rp86_public.MAX_REQUEST_BYTES, encoded)


_STATUS = struct.Struct("<IIIIIIIIHH16s")


def status_payload(
    *,
    workload_id: int,
    lifecycle: int,
    clock_mode: int = 0,
    cycles: int = 0,
    processor_flags: int = 0,
    result_flags: int = 0,
    completion_reason: int = 0,
    processor_signature: int = 0,
    output: bytes = b"",
) -> bytes:
    output = output[:16]
    return _STATUS.pack(
        workload_id,
        lifecycle,
        0,
        clock_mode,
        cycles,
        processor_flags,
        result_flags,
        completion_reason,
        processor_signature,
        len(output),
        output.ljust(16, b"\0"),
    )


class FakeWorkloadBroker:
    def __init__(self) -> None:
        self.requests: list[Message] = []

    def hello(self):
        return {
            "ok": True,
            "snapshot": {
                "request_sequence": 10,
                "workload_id": 0,
                "workload_state": "EMPTY",
            },
        }

    def exchange(self, record: bytes, request_id: str, timeout: float):
        request = Message.decode(record)
        self.requests.append(request)
        lifecycle = 2
        if request.message_type == 0x23 and request.payload:
            operation = request.payload[0]
            lifecycle = {1: 3, 2: 4, 3: 3, 4: 2}.get(operation, 2)
        reply = Message(
            TYPE_WORKLOAD_STATUS,
            request.sequence,
            status_payload(workload_id=7, lifecycle=lifecycle),
        )
        return {"ok": True, "reply_hex": reply.encode().hex(), "latency_ms": 1.0}


class PublicWorkloadApiTests(unittest.TestCase):
    def test_upload_raw_binary_uses_existing_workload_transport(self) -> None:
        api = WebApi(TOOLS)
        broker = FakeWorkloadBroker()
        with patch.object(api, "broker_client", return_value=(object(), broker)):
            result = api.workload_upload({
                "name": "hello.bin",
                "data": base64.b64encode(b"\x90\xf4").decode("ascii"),
                "address": "0x10000",
                "entry": "",
                "stack": "",
                "clock": "auto",
            })

        self.assertTrue(result["ok"])
        self.assertEqual(result["image_size"], 2)
        self.assertEqual(result["workload"]["state"], "STAGED")
        self.assertGreaterEqual(result["record_count"], 3)

    def test_run_uses_workload_control_record(self) -> None:
        api = WebApi(TOOLS)
        broker = FakeWorkloadBroker()
        with patch.object(api, "broker_client", return_value=(object(), broker)):
            result = api.workload_control("run")

        self.assertTrue(result["ok"])
        self.assertEqual(result["workload"]["state"], "RUNNING")
        self.assertEqual(broker.requests[-1].message_type, 0x23)


if __name__ == "__main__":
    unittest.main()
