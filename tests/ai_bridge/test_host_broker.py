from pathlib import Path
import json
import socket
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[0]
sys.path.insert(0, str(TOOLS))

from host_broker import (  # noqa: E402
    BrokerClient,
    BrokerRecord,
    BrokerState,
    DeviceBroker,
    discover_brokers,
    select_broker,
)


class HostBrokerTests(unittest.TestCase):
    def test_broker_fsm_rejects_invalid_transition(self) -> None:
        broker = DeviceBroker("FSM", "intel-8086")
        with self.assertRaisesRegex(RuntimeError, "invalid broker transition"):
            broker.transition(BrokerState.STOPPED)

    def test_select_broker_requires_identity_when_multiple_are_active(self) -> None:
        records = [
            BrokerRecord("ONE", 1001, 2001, 1, "intel-8086"),
            BrokerRecord("TWO", 1002, 2002, 2, "nec-v30"),
        ]
        with self.assertRaisesRegex(RuntimeError, "multiple"):
            select_broker(records)
        self.assertEqual(select_broker(records, "TWO"), records[1])
        self.assertIsNone(select_broker(records, "THREE"))

    def test_first_process_serves_tcp_and_is_discoverable(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"PI86_BROKER_DIR": directory}
        ):
            broker = DeviceBroker("SERIAL-ONE", "intel-8086")
            record = broker.start()
            try:
                discovered = discover_brokers()
                self.assertEqual(discovered, [record])
                reply = BrokerClient(record, "test-client").hello()
                self.assertTrue(reply["ok"])
                self.assertEqual(reply["device_id"], "SERIAL-ONE")
                self.assertEqual(reply["snapshot"]["state"], "OWNER_ACTIVE")
            finally:
                broker.stop()
            self.assertEqual(discover_brokers(), [])

    def test_device_actor_completes_queued_exchange(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"PI86_BROKER_DIR": directory}
        ):
            broker = DeviceBroker("QUEUE", "intel-8086")
            record = broker.start()
            result: dict[str, object] = {}

            def request() -> None:
                result.update(
                    BrokerClient(record, "client-a").exchange(
                        bytes(range(64)), "request-1", 1.0
                    )
                )

            thread = threading.Thread(target=request)
            thread.start()
            pending = broker.requests.get(timeout=1.0)
            self.assertEqual(pending.client_id, "client-a")
            self.assertEqual(pending.request_id, "request-1")
            pending.future.set_result(
                {"ok": True, "reply_hex": bytes(64).hex(), "latency_ms": 2.5}
            )
            thread.join(timeout=1.0)
            self.assertTrue(result["ok"])
            self.assertEqual(result["latency_ms"], 2.5)
            broker.stop()

    def test_cdc_control_is_queued_for_the_same_device_actor(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"PI86_BROKER_DIR": directory}
        ):
            broker = DeviceBroker("CONTROL", "intel-8086")
            record = broker.start()
            result: dict[str, object] = {}

            def request() -> None:
                result.update(
                    BrokerClient(record, "client-b").control(
                        "status", "control-1", 1.0
                    )
                )

            thread = threading.Thread(target=request)
            thread.start()
            pending = broker.controls.get(timeout=1.0)
            self.assertEqual(pending.command, "status")
            pending.future.set_result({"ok": True, "evidence_hex": b"STATUS".hex()})
            thread.join(timeout=1.0)
            self.assertTrue(result["ok"])
            broker.stop()

    def test_reboot_control_is_accepted_by_the_device_actor(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"PI86_BROKER_DIR": directory}
        ):
            broker = DeviceBroker("REBOOT", "intel-8086")
            record = broker.start()
            result: dict[str, object] = {}

            def request() -> None:
                result.update(
                    BrokerClient(record, "client-reboot").control(
                        "reboot", "control-reboot", 1.0
                    )
                )

            thread = threading.Thread(target=request)
            thread.start()
            pending = broker.controls.get(timeout=1.0)
            self.assertEqual(pending.command, "reboot")
            pending.future.set_result({"ok": True, "evidence_hex": ""})
            thread.join(timeout=1.0)
            self.assertTrue(result["ok"])
            broker.stop()

    def test_udp_telemetry_is_read_only_fanout(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.dict(
            "os.environ", {"PI86_BROKER_DIR": directory}
        ):
            broker = DeviceBroker("UDP", "nec-v30")
            record = broker.start()
            listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            listener.bind(("127.0.0.1", 0))
            listener.settimeout(1.0)
            try:
                port = listener.getsockname()[1]
                self.assertTrue(BrokerClient(record, "observer").subscribe(port)["ok"])
                broker.publish({"state": "OWNER_ACTIVE", "sequence": 42})
                payload, _ = listener.recvfrom(4096)
                telemetry = json.loads(payload.decode("utf-8"))
                self.assertEqual(telemetry["device_id"], "UDP")
                self.assertEqual(telemetry["snapshot"]["sequence"], 42)
            finally:
                listener.close()
                broker.stop()


if __name__ == "__main__":
    unittest.main()
