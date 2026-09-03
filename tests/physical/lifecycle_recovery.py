"""Opt-in physical lifecycle checks; replaces the current workload, never flashes.

Evidence is structured HID state through the broker, not a raw CDC capture.
"""

import argparse
from datetime import datetime
from pathlib import Path
import secrets
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.core import validate_device_reply
from rp86_runtime.device import DeviceClient
from rp86_runtime.runtime_state import RequestSequence, WorkloadRuntimeState
from rp86_runtime.session_evidence import SessionEvidence
from rp86_runtime.service_client import RuntimeServiceClient
from rp86_runtime.workload import (
    CLOCK_MODES, FLAG_CLOCK_STEPPED, WorkloadManifest, control_record, upload_records,
    workload_from_bytes,
)
from rp86_runtime.workload_client import WorkloadClient
from rp86_web_api import WebApi


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--persistent", type=Path, required=True)
    parser.add_argument("--recovery", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--diagnostics", action="store_true",
                        help="also verify stopped-executor diagnostics (requires updated firmware)")
    parser.add_argument("--execution-deadline", action="store_true",
                        help="also verify firmware-owned execution limits; restores the initial setting")
    args = parser.parse_args()
    persistent = args.persistent.read_bytes()
    recovery = args.recovery.read_bytes()
    api = WebApi(ROOT / "tools")
    evidence = SessionEvidence.create(args.output_dir, datetime.now().astimezone())
    state = WorkloadRuntimeState()
    cases = []
    failure = None
    client = None
    initial_timeout_ms = None
    try:
        owner = api.ensure_runtime_owner(wait_seconds=15, allow_reboot_recovery=False)
        if not owner.get("ok"):
            raise RuntimeError(str(owner))
        device = DeviceClient.discover()
        sequence = RequestSequence(1)

        def exchange(request):
            began = time.monotonic()
            try:
                reply = validate_device_reply(device.exchange(request.encode()), request)
                return reply, (time.monotonic() - began) * 1000, None
            except (RuntimeError, ValueError, OSError) as exc:
                return None, 0.0, str(exc)

        client = WorkloadClient(exchange, sequence, state,
                                lambda: evidence.observe_workload(state), lambda: None)
        services = RuntimeServiceClient(exchange, sequence, lambda: None)

        def execution_limit(milliseconds=None):
            setting, error = services.execution_timeout(milliseconds)
            assert setting is not None, error
            evidence.record({"event": "execution_timeout", **setting.__dict__})
            return setting

        if args.execution_deadline:
            initial_timeout_ms = execution_limit().timeout_ms
            execution_limit(0)

        def diagnose(expected_address=None):
            if not args.diagnostics:
                return
            snapshot, error = services.read_diagnostics(state.workload_id)
            assert snapshot is not None, error
            assert snapshot.lifecycle == state.lifecycle and snapshot.cycles == state.cycles
            assert snapshot.completion_reason == state.completion_reason
            if expected_address is not None:
                assert snapshot.as_dict()["last_address"] == expected_address, snapshot
                assert snapshot.as_dict()["cycle_name"] == "MEM_READ", snapshot
                assert snapshot.as_dict()["fault_flags"] == ["UNMAPPED"], snapshot
                assert snapshot.as_dict()["last_data"] is None, snapshot
            again, error = services.read_diagnostics(state.workload_id)
            assert again == snapshot, error
            evidence.retain_diagnostics(snapshot)
            print(snapshot.format(), flush=True)

        def transact(records):
            result = client.transact(records)
            if not result.success:
                raise AssertionError(result.error)

        def control(operation):
            transact([control_record(operation, workload_id=0, sequence=sequence.value)])

        def load(source, data, raw=False):
            control("status")
            if state.upload_requires_stop:
                control("stop")
            if raw:
                manifest = WorkloadManifest.for_image(
                    data, load_address=0x10000, entry_segment=0x1000,
                    entry_offset=0, flags=FLAG_CLOCK_STEPPED,
                )
                records = upload_records(manifest, data, transfer_id=secrets.randbits(32),
                                         first_sequence=sequence.value)
            else:
                manifest, _, records = workload_from_bytes(
                    data, [source], transfer_id=secrets.randbits(32),
                    first_sequence=sequence.value,
                )
            transact(records)
            evidence.bind_workload(state.workload_id, source, manifest)
            if args.diagnostics:
                snapshot, error = services.read_diagnostics(state.workload_id)
                assert snapshot is None and "no stopped" in error, (snapshot, error)

        def fresh_start(operation):
            control(operation)
            assert state.lifecycle == 3, state
            assert state.completion_reason == 0, f"stale completion reason after {operation}: {state}"
            assert state.result_flags & 7 == 0, f"stale native result after {operation}: {state}"
            assert not state.native_output, state

        def await_state(expected):
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                control("status")
                if state.lifecycle == expected:
                    return
                if state.lifecycle in (5, 6, 7):
                    raise AssertionError(f"expected lifecycle {expected}, got {state}")
                time.sleep(0.02)
            raise AssertionError(f"deadline awaiting lifecycle {expected}: {state}")

        def passed(name):
            cases.append({"case": name, "status": evidence.workload_snapshot(state)})
            print(f"PASS: {name}", flush=True)

        def recover(name):
            load(str(args.recovery), recovery)
            fresh_start("run")
            await_state(5)
            assert state.physical_regression_passed, state
            passed(name)

        load(str(args.persistent), persistent)
        fresh_start("run")
        time.sleep(0.05)
        control("status")
        first_cycles = state.cycles
        time.sleep(0.05)
        control("status")
        assert state.lifecycle == 3 and state.cycles > first_cycles, state
        if args.diagnostics:
            snapshot, error = services.read_diagnostics(state.workload_id)
            assert snapshot is None and "processor is executing" in error, (snapshot, error)
        control("stop")
        assert state.lifecycle == 4 and state.clock_mode == CLOCK_MODES["stopped"], state
        assert state.processor_state == "STOPPED / RESET", state
        assert state.completion_reason == 1, state
        stopped_cycles = state.cycles
        time.sleep(0.05)
        control("status")
        assert state.cycles == stopped_cycles, state
        passed("stop while executing")
        fresh_start("run")
        control("stop")
        passed("run after stop")
        recover("completion before restart")
        fresh_start("restart")
        await_state(5)
        assert state.physical_regression_passed, state
        passed("restart after completion")

        # CLI; HLT without IDLE_PREPARE is a bus fault on this physical path,
        # not proof of the no-ALE timeout (covered by the executor unit test).
        load("fixture:unannounced-hlt", bytes.fromhex("fa f4"), raw=True)
        fresh_start("run")
        await_state(6)
        assert state.completion_reason == 4 and state.clock_mode == CLOCK_MODES["stopped"], state
        assert state.processor_state == "STOPPED / RESET", state
        passed("unannounced HLT fault parks processor")
        diagnose()
        recover("load/run after unannounced HLT fault")

        # MOV AX,4000h; MOV DS,AX; MOV AX,[0000h]; HLT.
        # Read 40000h, outside the 256 KiB backing. No external I/O writes.
        load("fixture:unmapped-read", bytes.fromhex("b8 00 40 8e d8 a1 00 00 f4"), raw=True)
        fresh_start("run")
        await_state(6)
        assert state.completion_reason == 4 and state.clock_mode == CLOCK_MODES["stopped"], state
        assert state.processor_state == "STOPPED / RESET", state
        passed("unmapped memory fault parks processor")
        diagnose(0x40000)
        recover("load/run after fault")
        if args.execution_deadline:
            load(str(args.persistent), persistent)
            execution_limit(400)
            fresh_start("run")
            # No requests/polls during this interval: enforcement belongs to RP2350.
            time.sleep(0.7)
            await_state(7)
            assert state.completion_reason == 6 and state.cycles > 0, state
            assert state.processor_state == "STOPPED / RESET", state
            assert execution_limit().armed == 0
            snapshot, error = services.read_diagnostics(state.workload_id)
            assert snapshot is not None and snapshot.completion_reason == 6, error
            evidence.retain_diagnostics(snapshot)
            passed("busy loop expires without Host polling")

            execution_limit(5000)
            recover("load/run after execution deadline")
            assert execution_limit().armed == 0
            fresh_start("restart")
            await_state(5)
            assert state.physical_regression_passed, state
            passed("restart gets a new deadline and completes")

            load(str(args.persistent), persistent)
            execution_limit(0)
            fresh_start("run")
            time.sleep(0.7)
            control("status")
            assert state.lifecycle == 3 and state.cycles > 0, state
            control("stop")
            assert execution_limit().armed == 0
            passed("timeout off allows persistent execution until stop")
            recover("normal workload after disabling limit")
    except Exception as exc:
        failure = str(exc)
        evidence.failure("physical lifecycle", failure)
        print(f"FAIL: {failure}", file=sys.stderr)
    finally:
        # Stop only if the test left native execution running; no reboot fallback.
        if client is not None and state.lifecycle == 3:
            try:
                control("stop")
            except Exception as exc:
                evidence.failure("cleanup stop", str(exc))
        if initial_timeout_ms is not None:
            try:
                execution_limit(initial_timeout_ms)
            except Exception as exc:
                failure = failure or f"cannot restore execution limit: {exc}"
                evidence.failure("restore execution limit", str(exc))
        evidence.write({"schema": "rp86.lifecycle-recovery/v1", "passed": failure is None,
                        "failure_reason": failure, "cases": cases,
                        "workload": evidence.workload_snapshot(state)})
        api.stop_owned_runtime()
        print(f"Evidence: {evidence.json_path}")
    return 0 if failure is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
