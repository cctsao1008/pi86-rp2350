"""Broker/device API boundary for the local RP86 Web console."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

from rp86_runtime.broker import BrokerClient, discover_brokers, select_broker
from rp86_runtime.core import Message, NativeServiceWitness, TYPE_COMMAND
from rp86_runtime.memory import (
    format_memory_dump,
    memory_read_request,
    parse_memory_read,
)
from rp86_web_view import processor_view


class WebApi:
    OWNER_STARTUP_WAIT_S = 75.0
    REBOOT_SETTLE_S = 1.5

    def __init__(self, tools_root: Path) -> None:
        self.tools_root = tools_root
        self.rp86 = tools_root / "rp86.py"
        self.owned_runtime: subprocess.Popen[str] | None = None
        self.owner_mode = "not-started"
        self.owner_error: str | None = None
        self.recovery_lock = threading.Lock()
        self.recovery_active = threading.Event()

    def run_rp86(self, *args: str, timeout: float = 8.0) -> dict[str, object]:
        command = [sys.executable, str(self.rp86), *args]
        try:
            completed = subprocess.run(
                command, cwd=str(self.tools_root.parent), capture_output=True,
                text=True, timeout=timeout, check=False,
            )
        except subprocess.TimeoutExpired as exc:
            return {"ok": False, "error": f"RP86 command timed out after {timeout:.1f}s",
                    "stdout": exc.stdout or "", "stderr": exc.stderr or ""}
        return {"ok": completed.returncode == 0,
                "returncode": completed.returncode,
                "stdout": completed.stdout, "stderr": completed.stderr,
                "command": command[2:]}

    @staticmethod
    def active_broker():
        return select_broker(discover_brokers())

    def ensure_runtime_owner(
        self, wait_seconds: float | None = None,
        allow_reboot_recovery: bool = True,
    ) -> dict[str, object]:
        wait = self.OWNER_STARTUP_WAIT_S if wait_seconds is None else wait_seconds
        try:
            record = self.active_broker()
        except RuntimeError as exc:
            self.owner_error = str(exc)
            return {"ok": False, "error": self.owner_error}
        if record is not None:
            self.owner_mode, self.owner_error = "existing", None
            return {"ok": True, "mode": self.owner_mode,
                    "device_id": record.device_id}

        command = [sys.executable, str(self.rp86), "--interactive", "--attach",
                   "--display", "quiet", "--interval", "1.0"]
        try:
            self.owned_runtime = subprocess.Popen(
                command, cwd=str(self.tools_root.parent),
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, text=True,
            )
        except OSError as exc:
            self.owner_error = f"failed to start background RP86 runtime: {exc}"
            return {"ok": False, "error": self.owner_error}

        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            if self.owned_runtime.poll() is not None:
                exit_code = self.owned_runtime.returncode
                if allow_reboot_recovery:
                    reboot = self.run_rp86("--reboot", "--timeout", "5", timeout=8.0)
                    if reboot.get("ok"):
                        self.owned_runtime = None
                        time.sleep(self.REBOOT_SETTLE_S)
                        return self.ensure_runtime_owner(wait, False)
                    recovery = str(reboot.get("error") or reboot.get("stderr") or
                                   "HID reboot failed").strip()
                    self.owner_error = (
                        "background RP86 runtime exited before publishing a Host "
                        f"Broker (exit {exit_code}); recovery failed: {recovery}")
                    return {"ok": False, "error": self.owner_error}
                self.owner_error = (
                    "background RP86 runtime exited before publishing a Host Broker "
                    f"(exit {exit_code})")
                return {"ok": False, "error": self.owner_error}
            try:
                record = self.active_broker()
            except RuntimeError as exc:
                self.owner_error = str(exc)
                return {"ok": False, "error": self.owner_error}
            if record is not None:
                self.owner_mode, self.owner_error = "web-owned", None
                return {"ok": True, "mode": self.owner_mode,
                        "device_id": record.device_id}
            time.sleep(0.1)
        self.owner_mode = "starting"
        self.owner_error = "background RP86 runtime is still starting; no Host Broker yet"
        return {"ok": False, "error": self.owner_error}

    def stop_owned_runtime(self) -> None:
        process = self.owned_runtime
        if process is None or process.poll() is not None:
            return
        process.terminate()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2.0)
        self.owned_runtime = None

    def start_runtime_recovery(self, reason: str) -> None:
        if self.owner_mode != "web-owned" or self.recovery_active.is_set():
            return
        with self.recovery_lock:
            if self.recovery_active.is_set():
                return
            self.recovery_active.set()
        self.owner_error = f"RP86 runtime restarting: {reason}"

        def recover() -> None:
            try:
                time.sleep(self.REBOOT_SETTLE_S)
                process = self.owned_runtime
                if process is not None and process.poll() is None:
                    try:
                        process.wait(timeout=5.0)
                    except subprocess.TimeoutExpired:
                        pass
                if process is not None and process.poll() is not None:
                    self.owned_runtime = None
                result = self.ensure_runtime_owner()
                if not result.get("ok"):
                    self.owner_error = str(result.get("error") or
                                           "RP86 runtime recovery failed")
            finally:
                self.recovery_active.clear()
        threading.Thread(target=recover, name="rp86-web-runtime-recovery",
                         daemon=True).start()

    def processor_snapshot(self) -> dict[str, object]:
        try:
            record = self.active_broker()
        except RuntimeError as exc:
            return {"ok": False, "error": str(exc),
                    "owner_mode": self.owner_mode}
        if record is None:
            process = self.owned_runtime
            if (self.owner_mode == "web-owned" and
                    (process is None or process.poll() is not None)):
                self.start_runtime_recovery("Web-owned processor session exited")
            return {"ok": False,
                    "error": self.owner_error or "No active RP86 Host Broker.",
                    "owner_mode": self.owner_mode}
        try:
            reply = BrokerClient(record, f"web-{os.getpid()}").hello()
        except (OSError, RuntimeError, ValueError) as exc:
            return {"ok": False, "error": f"Host Broker telemetry failed: {exc}"}
        if not reply.get("ok"):
            return {"ok": False,
                    "error": str(reply.get("error") or "broker hello failed")}
        return processor_view(owner_mode=self.owner_mode, record=record, reply=reply)

    def broker_client(self, prefix: str):
        record = self.active_broker()
        if record is None:
            return None, None
        return record, BrokerClient(record, f"{prefix}-{os.getpid()}")

    def processor_command(self, text: str, timeout: float = 3.0) -> dict[str, object]:
        payload = text.encode("utf-8")
        if not payload:
            return {"ok": False, "error": "command is empty"}
        if len(payload) > 14:
            return {"ok": False, "error": "command exceeds the 14-byte native mailbox limit"}
        try:
            record, client = self.broker_client("web-console")
        except RuntimeError as exc:
            return {"ok": False, "error": str(exc)}
        if record is None or client is None:
            return {"ok": False, "error": "No active RP86 Host Broker."}
        last_error = "processor command failed"
        for attempt in range(2):
            try:
                hello = client.hello()
                if not hello.get("ok"):
                    raise RuntimeError(str(hello.get("error") or "broker hello failed"))
                sequence = int(dict(hello.get("snapshot") or {}).get("request_sequence") or 1) & 0xFFFFFFFF or 1
                request = Message(TYPE_COMMAND, sequence, payload)
                result = client.exchange(request.encode(), f"web-console-{os.getpid()}-{time.time_ns()}-{attempt}", timeout)
                if not result.get("ok"):
                    last_error = str(result.get("error") or last_error)
                    continue
                reply = Message.decode(bytes.fromhex(str(result["reply_hex"])))
                witness = NativeServiceWitness.decode(reply.payload)
                return {"ok": True, "processor": witness.processor,
                        "reply": witness.text.decode("ascii", errors="replace"),
                        "request_sequence": sequence, "boot_id": witness.boot_id,
                        "cpu_sequence": witness.cpu_sequence,
                        "command_sequence": witness.command_sequence,
                        "latency_ms": float(result.get("latency_ms") or 0.0)}
            except (OSError, RuntimeError, ValueError, KeyError) as exc:
                last_error = str(exc)
                if attempt == 0:
                    time.sleep(0.03)
        return {"ok": False, "error": last_error}

    def memory_read(self, address_value: object, length_value: object,
                    timeout: float = 3.0) -> dict[str, object]:
        try:
            address, length = int(str(address_value), 0), int(str(length_value), 0)
        except (TypeError, ValueError):
            return {"ok": False, "error": "address and length must be integers"}
        if not 1 <= length <= 40:
            return {"ok": False, "error": "memory viewer length must be 1-40 bytes"}
        try:
            record, client = self.broker_client("web-memory")
        except RuntimeError as exc:
            return {"ok": False, "error": str(exc)}
        if record is None or client is None:
            return {"ok": False, "error": "No active RP86 Host Broker."}
        last_error = "memory read failed"
        for attempt in range(2):
            try:
                hello = client.hello()
                if not hello.get("ok"):
                    raise RuntimeError(str(hello.get("error") or "broker hello failed"))
                sequence = int(dict(hello.get("snapshot") or {}).get("request_sequence") or 1) & 0xFFFFFFFF or 1
                request = memory_read_request(address, length, sequence)
                result = client.exchange(request.encode(), f"web-memory-{os.getpid()}-{time.time_ns()}-{attempt}", timeout)
                if not result.get("ok"):
                    last_error = str(result.get("error") or last_error)
                    continue
                reply = Message.decode(bytes.fromhex(str(result["reply_hex"])))
                data = parse_memory_read(reply, request)
                return {"ok": True, "address": address, "length": len(data),
                        "hex": data.hex(), "dump": format_memory_dump(address, data),
                        "request_sequence": sequence,
                        "latency_ms": float(result.get("latency_ms") or 0.0)}
            except (OSError, RuntimeError, ValueError, KeyError) as exc:
                last_error = str(exc)
                if attempt == 0:
                    time.sleep(0.03)
        return {"ok": False, "error": last_error}

    def get(self, path: str) -> tuple[dict[str, object], int]:
        if path == "/api/processor":
            result = self.processor_snapshot()
            return result, 200 if result["ok"] else 503
        args = {"/api/status": (("--status", "--timeout", "3"), 5.0),
                "/api/devices": (("--list-devices", "--json"), 5.0),
                "/api/simulate": (("--simulate", "--json"), 3.0)}
        if path not in args:
            return {"ok": False, "error": "not found"}, 404
        command, timeout = args[path]
        result = self.run_rp86(*command, timeout=timeout)
        if path == "/api/status" or not result.get("ok"):
            return result, 200 if result.get("ok") else 503
        try:
            value = json.loads(str(result["stdout"]))
        except json.JSONDecodeError as exc:
            return {"ok": False, "error": f"invalid JSON: {exc}"}, 500
        return ({"ok": True, "devices": value} if path == "/api/devices" else
                {"ok": True, "result": value}), 200

    def post(self, path: str, payload: dict[str, object]) -> tuple[dict[str, object], int]:
        if path == "/api/console":
            text = payload.get("text")
            if not isinstance(text, str):
                return {"ok": False, "error": "text must be a string"}, 400
            result = self.processor_command(text)
            return result, 200 if result["ok"] else 503
        if path == "/api/memory":
            result = self.memory_read(payload.get("address"), payload.get("length"))
            validation = any(x in str(result.get("error")) for x in ("integer", "1-40", "Internal SRAM"))
            return result, 200 if result["ok"] else 400 if validation else 503
        if path != "/api/control":
            return {"ok": False, "error": "not found"}, 404
        action = payload.get("action")
        if action not in {"reboot", "bootloader"}:
            return {"ok": False, "error": "unsupported control action"}, 400
        result = self.run_rp86("--reboot" if action == "reboot" else "--bootloader",
                               "--timeout", "5", timeout=8.0)
        if action == "reboot" and result.get("ok"):
            self.start_runtime_recovery("RP2350 reboot acknowledged")
        return result, 200 if result["ok"] else 503
