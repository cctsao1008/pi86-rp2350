"""Deploy and start one persistent processor workload without waiting for completion."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time

from .broker import BrokerClient, BrokerRecord, discover_brokers, select_broker
from .constants import PASS_EXIT, TRANSPORT_EXIT, VALIDATION_EXIT
from .protocol import Message, STATUS_OK
from .runtime_state import WorkloadRuntimeState, workload_upload_requires_stop
from .workload import control_record, workload_from_bytes


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "deploy and start one physical processor workload, then return while "
            "the processor keeps running"
        )
    )
    parser.add_argument("--start-workload", metavar="P86W", required=True)
    parser.add_argument("--hid-serial")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--owner-wait", type=float, default=5.0)
    return parser


def _next_sequence(sequence: int) -> int:
    return (sequence + 1) & 0xFFFFFFFF or 1


def _sequence_from_hello(hello: dict[str, object]) -> int:
    sequence = int(
        dict(hello.get("snapshot") or {}).get("request_sequence") or 1
    )
    return sequence & 0xFFFFFFFF or 1


def _workload_path_error(value: str) -> str | None:
    path = Path(value)
    if not path.is_file():
        return f"workload not found: {value}"
    if path.suffix.lower() != ".p86w":
        return "--start-workload currently requires a .P86W workload"
    return None


def _spawn_background_owner() -> subprocess.Popen[bytes]:
    tools_root = Path(__file__).resolve().parents[1]
    command = [
        sys.executable,
        str(tools_root / "rp86.py"),
        "--interactive",
        "--attach",
        "--display",
        "quiet",
        "--interval",
        "1.0",
    ]
    kwargs: dict[str, object] = {
        "cwd": str(tools_root.parent),
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.DEVNULL,
        "stderr": subprocess.DEVNULL,
    }
    if os.name == "nt":
        kwargs["creationflags"] = (
            subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
        )
    else:
        kwargs["start_new_session"] = True
    return subprocess.Popen(command, **kwargs)


def _ensure_runtime_owner(
    device_id: str | None,
    wait_seconds: float,
) -> tuple[BrokerRecord | None, subprocess.Popen[bytes] | None, str | None]:
    try:
        record = select_broker(discover_brokers(), device_id)
    except RuntimeError as exc:
        return None, None, str(exc)
    if record is not None:
        return record, None, None

    try:
        process = _spawn_background_owner()
    except OSError as exc:
        return None, None, f"failed to start background RP86 runtime: {exc}"

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return (
                None,
                process,
                "background RP86 runtime exited before publishing a Host Broker "
                f"(exit {process.returncode})",
            )
        try:
            record = select_broker(discover_brokers(), device_id)
        except RuntimeError as exc:
            return None, process, str(exc)
        if record is not None:
            return record, process, None
        time.sleep(0.1)
    return None, process, "background RP86 runtime did not publish a Host Broker in time"


def _exchange(
    client: BrokerClient,
    request: Message,
    operation: str,
    timeout: float,
) -> tuple[Message | None, str | None]:
    try:
        result = client.exchange(
            request.encode(),
            f"start-workload-{operation}-{os.getpid()}-{time.time_ns()}",
            timeout,
        )
        if not result.get("ok"):
            return None, str(result.get("error") or f"{operation} failed")
        reply = Message.decode(bytes.fromhex(str(result["reply_hex"])))
        if reply.status != STATUS_OK:
            return None, f"{operation} rejected by RP2350 (status {reply.status})"
        return reply, None
    except (OSError, RuntimeError, ValueError, KeyError) as exc:
        return None, str(exc)


def _state(reply: Message) -> WorkloadRuntimeState:
    return WorkloadRuntimeState.from_payload(reply.payload)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.timeout <= 0:
        raise SystemExit("--timeout must be greater than zero")
    if args.owner_wait <= 0:
        raise SystemExit("--owner-wait must be greater than zero")

    path_error = _workload_path_error(args.start_workload)
    if path_error is not None:
        print(f"ERROR: {path_error}", file=sys.stderr)
        return VALIDATION_EXIT

    print("\n[RP86 WORKLOAD START]")
    print(f"Workload = {args.start_workload}")

    record, _spawned_owner, owner_error = _ensure_runtime_owner(
        args.hid_serial, args.owner_wait
    )
    if record is None:
        print(f"ERROR: {owner_error or 'no active RP86 Host broker'}", file=sys.stderr)
        return TRANSPORT_EXIT

    client = BrokerClient(
        record, f"start-workload-{os.getpid()}-{secrets.token_hex(4)}"
    )
    try:
        hello = client.hello()
        if not hello.get("ok"):
            raise RuntimeError(str(hello.get("error") or "broker hello failed"))
        sequence = _sequence_from_hello(hello)

        status_request = control_record(
            "status", workload_id=0, sequence=sequence
        )
        status_reply, error = _exchange(
            client, status_request, "status", args.timeout
        )
        if status_reply is None:
            raise RuntimeError(error or "workload status failed")
        current = _state(status_reply)
        sequence = _next_sequence(status_request.sequence)

        if workload_upload_requires_stop(current.lifecycle):
            print("load: stopping active processor")
            stop_request = control_record(
                "stop", workload_id=0, sequence=sequence
            )
            stop_reply, error = _exchange(
                client, stop_request, "stop-before-load", args.timeout
            )
            if stop_reply is None:
                raise RuntimeError(error or "workload stop failed")
            sequence = _next_sequence(stop_request.sequence)

        encoded = Path(args.start_workload).read_bytes()
        manifest, image, records = workload_from_bytes(
            encoded,
            (args.start_workload,),
            transfer_id=secrets.randbits(32),
            first_sequence=sequence,
        )
        print(
            "Native workload upload\n"
            f"  image   {len(image)} bytes\n"
            f"  address 0x{manifest.load_address:05X}\n"
            f"  entry   {manifest.entry_segment:04X}:{manifest.entry_offset:04X}\n"
            f"  CRC32   {manifest.image_crc32:08X}"
        )

        final_reply: Message | None = None
        for index, request in enumerate(records, 1):
            final_reply, error = _exchange(
                client, request, f"load-{index}", args.timeout
            )
            if final_reply is None:
                print(
                    f"workload upload: FAILED at record {index}/{len(records)}: "
                    f"{error or 'unknown error'}",
                    file=sys.stderr,
                )
                return VALIDATION_EXIT
        assert final_reply is not None
        staged = _state(final_reply)
        print(
            f"workload upload: PASS ({len(records)} records)\n"
            f"  workload_id={staged.workload_id} state={staged.lifecycle_name} "
            f"detail={staged.detail} clock={staged.clock_name} "
            f"cycles={staged.cycles} processor={staged.processor_state}"
        )

        sequence = _next_sequence(records[-1].sequence)
        run_request = control_record(
            "run", workload_id=0, sequence=sequence
        )
        run_reply, error = _exchange(client, run_request, "run", args.timeout)
        if run_reply is None:
            raise RuntimeError(error or "workload run failed")
        running = _state(run_reply)
        print(
            "workload run: ACCEPTED (1 records)\n"
            f"  workload_id={running.workload_id} state={running.lifecycle_name} "
            f"detail={running.detail} clock={running.clock_name} "
            f"cycles={running.cycles} processor={running.processor_state}"
        )

        if running.lifecycle_name != "RUNNING" or running.processor_state != "ACTIVE":
            print(
                "WORKLOAD START: FAIL "
                f"(state={running.lifecycle_name}, processor={running.processor_state})",
                file=sys.stderr,
            )
            return VALIDATION_EXIT

        print("WORKLOAD START: PASS")
        print("Physical processor continues executing after Host command return.")
        return PASS_EXIT
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"WORKLOAD START: FAIL ({exc})", file=sys.stderr)
        return TRANSPORT_EXIT


if __name__ == "__main__":
    raise SystemExit(main())
