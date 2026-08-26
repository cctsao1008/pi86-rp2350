#!/usr/bin/env python3
"""Local Host broker for one physical pi86-rp2350 device.

The broker never touches hardware from its network thread.  TCP requests are
placed on a queue and completed by the single Device Actor that already owns
CDC/HID.  UDP carries read-only telemetry snapshots.
"""

from __future__ import annotations

import asyncio
import base64
from concurrent.futures import Future
from dataclasses import dataclass
from enum import Enum
import json
import os
from pathlib import Path
import queue
import socket
import tempfile
import threading
from typing import Any


BROKER_VERSION = 1
MAX_RPC_BYTES = 16 * 1024


class BrokerState(str, Enum):
    OPENING = "OPENING"
    OWNER_ACTIVE = "OWNER_ACTIVE"
    QUIESCING = "QUIESCING"
    FAULT = "FAULT"
    STOPPED = "STOPPED"


BROKER_TRANSITIONS = {
    BrokerState.OPENING: {BrokerState.OWNER_ACTIVE, BrokerState.FAULT},
    BrokerState.OWNER_ACTIVE: {BrokerState.QUIESCING, BrokerState.FAULT},
    BrokerState.FAULT: {BrokerState.QUIESCING},
    BrokerState.QUIESCING: {BrokerState.STOPPED},
    BrokerState.STOPPED: set(),
}


def broker_registry_dir() -> Path:
    configured = os.environ.get("PI86_BROKER_DIR")
    if configured:
        return Path(configured).expanduser()
    return Path(tempfile.gettempdir()) / "pi86-rp2350-brokers"


@dataclass(frozen=True)
class BrokerRecord:
    device_id: str
    tcp_port: int
    udp_port: int
    pid: int
    processor: str

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "BrokerRecord":
        if value.get("version") != BROKER_VERSION:
            raise ValueError("unsupported broker registry version")
        return cls(
            device_id=str(value["device_id"]),
            tcp_port=int(value["tcp_port"]),
            udp_port=int(value["udp_port"]),
            pid=int(value["pid"]),
            processor=str(value["processor"]),
        )

    def as_dict(self) -> dict[str, Any]:
        return {
            "version": BROKER_VERSION,
            "device_id": self.device_id,
            "tcp_port": self.tcp_port,
            "udp_port": self.udp_port,
            "pid": self.pid,
            "processor": self.processor,
        }


@dataclass
class BrokerExchangeRequest:
    client_id: str
    request_id: str
    record: bytes
    future: Future[dict[str, Any]]


@dataclass
class BrokerControlRequest:
    client_id: str
    request_id: str
    command: str
    timeout: float
    future: Future[dict[str, Any]]


def _registry_path(device_id: str) -> Path:
    safe = "".join(char if char.isalnum() or char in "-_" else "_" for char in device_id)
    return broker_registry_dir() / f"{safe}.json"


def _rpc(record: BrokerRecord, payload: dict[str, Any], timeout: float = 0.5) -> dict[str, Any]:
    encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"
    with socket.create_connection(("127.0.0.1", record.tcp_port), timeout=timeout) as connection:
        connection.settimeout(timeout)
        connection.sendall(encoded)
        response = bytearray()
        while len(response) <= MAX_RPC_BYTES:
            chunk = connection.recv(4096)
            if not chunk:
                break
            response.extend(chunk)
            if b"\n" in chunk:
                break
    line = bytes(response).split(b"\n", 1)[0]
    if not line:
        raise RuntimeError("broker returned no response")
    value = json.loads(line.decode("utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("broker returned an invalid response")
    return value


def discover_brokers() -> list[BrokerRecord]:
    directory = broker_registry_dir()
    if not directory.is_dir():
        return []
    records: list[BrokerRecord] = []
    for path in directory.glob("*.json"):
        try:
            record = BrokerRecord.from_dict(json.loads(path.read_text(encoding="utf-8")))
            reply = _rpc(record, {"version": BROKER_VERSION, "op": "hello"}, timeout=0.2)
            if reply.get("ok") and reply.get("device_id") == record.device_id:
                records.append(record)
        except (OSError, ValueError, KeyError, json.JSONDecodeError, RuntimeError):
            continue
    return records


def select_broker(
    records: list[BrokerRecord], device_id: str | None = None
) -> BrokerRecord | None:
    matches = records
    if device_id:
        matches = [record for record in matches if record.device_id == device_id]
    if not matches:
        return None
    if len(matches) == 1:
        return matches[0]
    detail = ", ".join(record.device_id for record in matches)
    raise RuntimeError(
        f"multiple pi86-rp2350 brokers are active: {detail}; use --hid-serial DEVICE_ID"
    )


class BrokerClient:
    def __init__(self, record: BrokerRecord, client_id: str) -> None:
        self.record = record
        self.client_id = client_id

    def hello(self) -> dict[str, Any]:
        return _rpc(
            self.record,
            {"version": BROKER_VERSION, "op": "hello", "client_id": self.client_id},
        )

    def exchange(self, record: bytes, request_id: str, timeout: float) -> dict[str, Any]:
        return _rpc(
            self.record,
            {
                "version": BROKER_VERSION,
                "op": "exchange",
                "client_id": self.client_id,
                "request_id": request_id,
                "record": base64.b64encode(record).decode("ascii"),
                "timeout": timeout,
            },
            timeout=timeout + 1.0,
        )

    def subscribe(self, udp_port: int) -> dict[str, Any]:
        return _rpc(
            self.record,
            {
                "version": BROKER_VERSION,
                "op": "subscribe",
                "client_id": self.client_id,
                "udp_port": udp_port,
            },
        )

    def control(self, command: str, request_id: str, timeout: float) -> dict[str, Any]:
        return _rpc(
            self.record,
            {
                "version": BROKER_VERSION,
                "op": "control",
                "client_id": self.client_id,
                "request_id": request_id,
                "command": command,
                "timeout": timeout,
            },
            timeout=timeout + 1.0,
        )


class _TelemetryProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.transport = transport


class DeviceBroker:
    """Network front end for a single-owner physical Device Actor."""

    def __init__(self, device_id: str, processor: str) -> None:
        self.device_id = device_id
        self.processor = processor
        self.requests: queue.Queue[BrokerExchangeRequest] = queue.Queue()
        self.controls: queue.Queue[BrokerControlRequest] = queue.Queue()
        self._subscribers: set[tuple[str, int]] = set()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._server: asyncio.AbstractServer | None = None
        self._udp_transport: asyncio.DatagramTransport | None = None
        self._thread: threading.Thread | None = None
        self._ready = threading.Event()
        self._stop_event: asyncio.Event | None = None
        self.record: BrokerRecord | None = None
        self.state = BrokerState.OPENING
        self.snapshot: dict[str, Any] = {
            "state": self.state.value,
            "completed": 0,
            "lost": 0,
            "sequence": 0,
        }

    def transition(self, target: BrokerState) -> None:
        if target not in BROKER_TRANSITIONS[self.state]:
            raise RuntimeError(
                f"invalid broker transition: {self.state.value} -> {target.value}"
            )
        self.state = target
        self.snapshot["state"] = target.value

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        response: dict[str, Any]
        try:
            line = await reader.readline()
            if not line or len(line) > MAX_RPC_BYTES:
                raise ValueError("invalid broker request length")
            request = json.loads(line.decode("utf-8"))
            if request.get("version") != BROKER_VERSION:
                raise ValueError("unsupported broker protocol version")
            operation = request.get("op")
            if operation == "hello":
                response = {
                    "ok": True,
                    "version": BROKER_VERSION,
                    "device_id": self.device_id,
                    "processor": self.processor,
                    "snapshot": dict(self.snapshot),
                }
            elif operation == "subscribe":
                peer = writer.get_extra_info("peername")
                udp_port = int(request["udp_port"])
                self._subscribers.add((peer[0], udp_port))
                response = {"ok": True, "device_id": self.device_id}
            elif operation == "exchange":
                record = base64.b64decode(request["record"], validate=True)
                if len(record) != 64:
                    raise ValueError("broker exchange requires one 64-byte ABI record")
                future: Future[dict[str, Any]] = Future()
                pending = BrokerExchangeRequest(
                    client_id=str(request.get("client_id") or "anonymous"),
                    request_id=str(request.get("request_id") or ""),
                    record=record,
                    future=future,
                )
                self.requests.put(pending)
                response = await asyncio.wrap_future(future)
            elif operation == "control":
                command = str(request["command"])
                if command not in ("status", "bootloader", "reboot"):
                    raise ValueError(f"unsupported broker control command: {command}")
                future = Future()
                control = BrokerControlRequest(
                    client_id=str(request.get("client_id") or "anonymous"),
                    request_id=str(request.get("request_id") or ""),
                    command=command,
                    timeout=float(request.get("timeout") or 5.0),
                    future=future,
                )
                self.controls.put(control)
                response = await asyncio.wrap_future(future)
            else:
                raise ValueError(f"unsupported broker operation: {operation}")
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
            response = {"ok": False, "error": str(exc)}
        writer.write(json.dumps(response, separators=(",", ":")).encode("utf-8") + b"\n")
        await writer.drain()
        writer.close()
        await writer.wait_closed()

    async def _run(self) -> None:
        self._loop = asyncio.get_running_loop()
        self._stop_event = asyncio.Event()
        self._server = await asyncio.start_server(
            self._handle_client, "127.0.0.1", 0
        )
        tcp_port = int(self._server.sockets[0].getsockname()[1])
        udp_transport, _ = await self._loop.create_datagram_endpoint(
            _TelemetryProtocol, local_addr=("127.0.0.1", 0)
        )
        self._udp_transport = udp_transport
        udp_port = int(udp_transport.get_extra_info("sockname")[1])
        self.record = BrokerRecord(
            device_id=self.device_id,
            tcp_port=tcp_port,
            udp_port=udp_port,
            pid=os.getpid(),
            processor=self.processor,
        )
        directory = broker_registry_dir()
        directory.mkdir(parents=True, exist_ok=True)
        path = _registry_path(self.device_id)
        temporary = path.with_suffix(".tmp")
        temporary.write_text(
            json.dumps(self.record.as_dict(), indent=2) + "\n", encoding="utf-8"
        )
        temporary.replace(path)
        self.transition(BrokerState.OWNER_ACTIVE)
        self._ready.set()
        async with self._server:
            await self._stop_event.wait()
        udp_transport.close()

    def start(self, timeout: float = 2.0) -> BrokerRecord:
        if self._thread is not None:
            raise RuntimeError("broker is already started")
        self._thread = threading.Thread(
            target=lambda: asyncio.run(self._run()),
            name=f"pi86-broker-{self.device_id}",
            daemon=True,
        )
        self._thread.start()
        if not self._ready.wait(timeout) or self.record is None:
            raise RuntimeError("broker did not start")
        return self.record

    def pending(self) -> list[BrokerExchangeRequest]:
        requests: list[BrokerExchangeRequest] = []
        while True:
            try:
                requests.append(self.requests.get_nowait())
            except queue.Empty:
                return requests

    def publish(self, snapshot: dict[str, Any]) -> None:
        self.snapshot = dict(snapshot)
        if self._loop is None or self._udp_transport is None:
            return
        payload = json.dumps(
            {
                "version": BROKER_VERSION,
                "device_id": self.device_id,
                "snapshot": self.snapshot,
            },
            separators=(",", ":"),
        ).encode("utf-8")

        def send() -> None:
            if self._udp_transport is None:
                return
            for subscriber in tuple(self._subscribers):
                self._udp_transport.sendto(payload, subscriber)

        self._loop.call_soon_threadsafe(send)

    def stop(self) -> None:
        if self.state in (BrokerState.OWNER_ACTIVE, BrokerState.FAULT):
            self.transition(BrokerState.QUIESCING)
        if self._loop is not None and self._stop_event is not None:
            self._loop.call_soon_threadsafe(self._stop_event.set)
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        if self.state == BrokerState.QUIESCING:
            self.transition(BrokerState.STOPPED)
        try:
            _registry_path(self.device_id).unlink(missing_ok=True)
        except OSError:
            pass
