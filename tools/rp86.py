#!/usr/bin/env python3
"""Canonical RP86 Host runtime entry point."""

import os
import sys
import threading
import time


_WEB_OWNER_ENV = "RP86_WEB_OWNER"
_WEB_OWNER_STARTUP_WAIT_S = 75.0
_WEB_REBOOT_SETTLE_S = 1.5


def _install_broker_workload_telemetry() -> None:
    """Enrich broker snapshots with the runtime's existing workload status.

    The persistent session already decodes every workload status/result record,
    but older broker snapshots expose only processor heartbeat telemetry.  Keep
    one process-local copy of the latest decoded workload state and merge it
    into DeviceBroker.publish().  This does not add USB traffic or change the
    wire protocol; it only exposes state the Host runtime already owns.
    """
    import rp86_runtime.session as session

    if getattr(session, "_broker_workload_telemetry_installed", False):
        return

    latest = {
        "workload_id": 0,
        "workload_state": "EMPTY",
        "workload_detail": 0,
        "workload_clock_mode": "AUTO",
        "workload_cycles": 0,
        "workload_processor_flags": 0,
        "workload_processor_state": "ACTIVE",
    }
    original_decode = session.decode_status_payload
    original_publish = session.DeviceBroker.publish

    def decode_status_payload(payload):
        decoded = original_decode(payload)
        (
            workload_id,
            workload_state,
            workload_detail,
            workload_clock_mode,
            workload_cycles,
            workload_processor_flags,
        ) = decoded
        latest.update(
            {
                "workload_id": workload_id,
                "workload_state": session._workload_state_name(workload_state),
                "workload_detail": workload_detail,
                "workload_clock_mode": session.CLOCK_MODE_NAMES.get(
                    workload_clock_mode, f"UNKNOWN({workload_clock_mode})"
                ),
                "workload_cycles": workload_cycles,
                "workload_processor_flags": workload_processor_flags,
                "workload_processor_state": session._processor_execution_state(
                    workload_clock_mode, workload_processor_flags
                ),
            }
        )
        return decoded

    def publish(broker, snapshot):
        enriched = dict(snapshot)
        enriched.update(latest)
        return original_publish(broker, enriched)

    session.decode_status_payload = decode_status_payload
    session.DeviceBroker.publish = publish
    session._broker_workload_telemetry_installed = True


def _rewrite_web_owned_runtime_args() -> None:
    """Use the proven exchange+heartbeat path for Web-owned background sessions.

    rp86_web.py launches a quiet background child with
    --interactive --heartbeat --attach.  When that child is descended from the
    Web entry point, replace the attach-only startup with the canonical
    physical exchange path so RESET/runtime acceptance and native processor
    identity are established before persistent heartbeat monitoring begins.

    The environment marker is set only by the --web parent, so normal manual
    CLI --attach semantics remain unchanged.
    """
    if os.environ.get(_WEB_OWNER_ENV) != "1":
        return
    required = {"--interactive", "--heartbeat", "--attach"}
    if not required.issubset(sys.argv):
        return
    sys.argv[:] = [
        arg
        for arg in sys.argv
        if arg not in {"--interactive", "--attach"}
    ]
    if "--exchange" not in sys.argv:
        sys.argv.insert(1, "--exchange")


def _web_main() -> int:
    """Run the Web console with a supervised Web-owned processor runtime.

    Initial startup waits for the full physical acceptance sequence before the
    HTTP server is exposed.  If the Web UI later reboots the RP2350, the owner
    process is expected to terminate because its broker intentionally quiesces
    after acknowledging the reboot.  The Web parent therefore supervises that
    child and automatically starts a fresh exchange+heartbeat owner after USB
    re-enumeration, instead of leaving the browser attached to a dead broker.
    """
    os.environ[_WEB_OWNER_ENV] = "1"
    import rp86_web

    original_ensure_runtime_owner = rp86_web._ensure_runtime_owner
    original_processor_snapshot = rp86_web._processor_snapshot
    original_run_rp86 = rp86_web._run_rp86

    recovery_lock = threading.Lock()
    recovery_active = threading.Event()

    def ensure_runtime_owner(wait_seconds: float = _WEB_OWNER_STARTUP_WAIT_S):
        return original_ensure_runtime_owner(wait_seconds=wait_seconds)

    def start_recovery(reason: str) -> None:
        """Start one asynchronous Web-owned runtime recovery attempt."""
        if rp86_web._owner_mode != "web-owned":
            return
        if recovery_active.is_set():
            return
        with recovery_lock:
            if recovery_active.is_set():
                return
            recovery_active.set()

        def recover() -> None:
            try:
                rp86_web._owner_error = f"RP86 runtime restarting: {reason}"
                time.sleep(_WEB_REBOOT_SETTLE_S)

                process = rp86_web._owned_runtime
                if process is not None and process.poll() is None:
                    try:
                        process.wait(timeout=5.0)
                    except Exception:
                        pass
                if process is not None and process.poll() is not None:
                    rp86_web._owned_runtime = None

                result = original_ensure_runtime_owner(
                    wait_seconds=_WEB_OWNER_STARTUP_WAIT_S
                )
                if not result.get("ok"):
                    rp86_web._owner_error = str(
                        result.get("error") or "RP86 runtime recovery failed"
                    )
            finally:
                recovery_active.clear()

        threading.Thread(
            target=recover,
            name="rp86-web-runtime-recovery",
            daemon=True,
        ).start()

    def run_rp86(*args: str, timeout: float = 8.0):
        """Wrap canonical CLI operations with Web-owner lifecycle handling."""
        if recovery_active.is_set() and "--status" in args:
            return {
                "ok": False,
                "error": "RP86 runtime is restarting after hardware reboot",
                "stdout": "",
                "stderr": "",
            }
        result = original_run_rp86(*args, timeout=timeout)
        if "--reboot" in args and result.get("ok"):
            start_recovery("RP2350 reboot acknowledged")
        return result

    def processor_snapshot():
        """Self-heal if a Web-owned broker disappears unexpectedly."""
        result = original_processor_snapshot()
        if result.get("ok"):
            return result
        process = rp86_web._owned_runtime
        owner_died = (
            rp86_web._owner_mode == "web-owned"
            and (process is None or process.poll() is not None)
        )
        if owner_died:
            start_recovery("Web-owned processor session exited")
        if recovery_active.is_set():
            result = dict(result)
            result["error"] = rp86_web._owner_error or "RP86 runtime is restarting"
            result["owner_mode"] = "web-owned"
        return result

    rp86_web._ensure_runtime_owner = ensure_runtime_owner
    rp86_web._run_rp86 = run_rp86
    rp86_web._processor_snapshot = processor_snapshot
    return rp86_web.main()


if "--web" in sys.argv:
    sys.argv.remove("--web")
    main = _web_main
else:
    _install_broker_workload_telemetry()
    _rewrite_web_owned_runtime_args()
    from rp86_runtime.cli import main


raise SystemExit(main())
