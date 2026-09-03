"""Persistent RP86 physical-processor session."""

from datetime import datetime
from dataclasses import asdict
import json
import os
from pathlib import Path
import queue
import re
import secrets
import sys
import time
from typing import Any

from .broker import (
    BrokerClient,
    BrokerRecord,
    DeviceBroker,
    discover_brokers,
    select_broker,
)
from .calculator import calculator_payload
from .console import (
    CdcDisplayStream,
    ConsoleStatus,
    _read_terminal_command,
)
from .constants import (
    PASS_EXIT,
    PROCESSOR_NAMES,
    TRANSPORT_EXIT,
    VALIDATION_EXIT,
)
from .core import (
    HeartbeatStats,
    heartbeat_payload,
    validate_device_reply,
)
from .filesystem import (
    df_request,
    parse_df,
    write_records,
)
from .mailbox import (
    MAILBOX_BASE,
    MAILBOX_DATA_SIZE,
    MailboxHeader,
    mailbox_commit_records,
)
from .memory import (
    format_memory_dump,
)
from .protocol import (
    Message,
    NativeServiceWitness,
    RUNTIME_CONTROL_ENTER_BOOTLOADER,
    RUNTIME_CONTROL_REBOOT,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_COMMIT,
    TYPE_WORKLOAD_CONTROL,
    TYPE_WORKLOAD_DATA,
    TYPE_WORKLOAD_RESULT,
    TYPE_WORKLOAD_STATUS,
)
from .request_channel import exchange_hid_request
from .runtime_state import (
    ProcessorObservationState,
    RequestSequence,
    RuntimeStatusSnapshot,
    WorkloadRuntimeState,
    processor_execution_state as _processor_execution_state,
    workload_state_name as _workload_state_name,
    workload_upload_requires_stop as _workload_upload_requires_stop,
)
from .service_client import RuntimeServiceClient
from .session_evidence import SessionEvidence, regression_failure_reasons
from .shell_commands import (
    CommandHistory,
    command_help,
    complete_shell_input,
    completion_token,
    format_host_directory,
    host_list_path,
    is_device_path,
    parse_command,
    unavailable_message,
)
from .transport import (
    _open_hid,
    _serial_module,
    exchange_hid_runtime_control,
    send_status_request,
)
from .workload import (
    CLOCK_MODE_NAMES,
    PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED,
    WorkloadManifest,
    control_record,
    upload_records,
    workload_from_bytes,
    workload_from_command,
)
from .workload_client import WorkloadClient
from .workload_timeout import parse_timeout


def _broker_runtime_state(transport_error: str | None) -> str:
    """RP2350 reachability is transport state, not processor probe state."""
    return "OWNER_ACTIVE" if transport_error is None else "FAULT"


def _native_probe_unavailable(error: str | None) -> bool:
    """Recognize an RP2350-completed diagnostic probe timeout."""
    return bool(error and error.endswith("status is not OK: 4"))


def _format_runtime_top(
    *,
    processor_name: str,
    processor_identified: bool,
    workload_id: int,
    workload_state: int,
    workload_detail: int,
    manifest: WorkloadManifest | None,
    workload_clock_mode: int = 0,
    workload_cycles: int = 0,
    workload_processor_flags: int = 0,
) -> str:
    """Present CPU-visible resources without exposing protocol state numbers."""
    state_name = _workload_state_name(workload_state)
    if workload_state in (1, 2):
        workload_text = (
            f"{state_name} id={workload_id} size={workload_detail} bytes"
        )
    else:
        workload_text = f"{state_name} id={workload_id} detail={workload_detail}"
    clock_name = CLOCK_MODE_NAMES.get(
        workload_clock_mode, f"UNKNOWN({workload_clock_mode})"
    )
    processor_clock = (
        "@ 1.000 MHz"
        if workload_clock_mode in (0, 1)
        else f"[{clock_name}]"
    )

    execution_state = _processor_execution_state(
        workload_clock_mode, workload_processor_flags
    )
    lines = [
        "Physical processor runtime top",
        f"  Processor  {processor_name} · {execution_state} {processor_clock}",
        "  Identity   " + (
            "NATIVE AAD 16 IDENTIFIED"
            if processor_identified else "NOT PROBED"
        ),
        f"  Workload   {workload_text}",
        f"  Clock mode {clock_name}",
        f"  CPU cycles {workload_cycles}",
        f"  Processor {execution_state}",
    ]
    if manifest is not None:
        lines.append(
            f"  Load       0x{manifest.load_address:05X} "
            f"entry={manifest.entry_segment:04X}:{manifest.entry_offset:04X}"
        )
    lines.extend(
        (
            "  Memory     INTERNAL SRAM 00000-3FFFF 256 KiB",
            "  PSRAM      NOT AVAILABLE (optional expansion)",
            "  flash:     RP-FLASH FAT16 AVAILABLE",
            "  sd:        NOT AVAILABLE",
        )
    )
    return "\n".join(lines)


def persistent_monitor(
    port: str,
    sequence: int,
    timeout: float,
    interval: float,
    output_dir: Path,
    serial_number: str | None,
    display: str,
    interactive: bool,
    rounds: int,
    processor: str,
    broker_record: BrokerRecord | None = None,
    native_probe: bool = False,
    regression_workload: str | None = None,
    regression_timeout: float = 60.0,
) -> int:
    """Keep one Host-owned runtime session synchronized with RP2350 state."""
    serial = _serial_module()
    started = datetime.now().astimezone()
    evidence = SessionEvidence.create(output_dir, started)
    probe_stats = HeartbeatStats()
    console = ConsoleStatus(processor, live=display not in ("plain",))
    cdc_display = CdcDisplayStream()
    processor_name = PROCESSOR_NAMES[processor]
    expected_processor = None if processor == "auto" else processor
    command_buffer = ""
    command_cursor = 0
    command_history = CommandHistory()
    host_cwd = Path.cwd()
    request_sequence = RequestSequence(sequence)
    processor_observation = ProcessorObservationState()
    workload = WorkloadRuntimeState()
    staged_workload_manifest: WorkloadManifest | None = None
    prepared_native_probe_available = True
    workload_completion_pending = False
    structured_result_reported: tuple[int, int, int] | None = None
    diagnostics_observed: tuple[int, int, int, int] | None = None
    regression_commands = (
        [f'load "{regression_workload}"', "run"]
        if regression_workload else []
    )
    regression_deadline = (
        time.monotonic() + regression_timeout
        if regression_workload else None
    )
    regression_started = False
    regression_passed: bool | None = None
    next_due = time.monotonic()
    stop = False
    transport_error: str | None = None

    connection = None
    hid_device = None
    owner_broker: DeviceBroker | None = None
    broker_client: BrokerClient | None = None
    if broker_record is not None:
        broker_client = BrokerClient(
            broker_record, f"pid-{os.getpid()}-{secrets.token_hex(4)}"
        )
        hello = broker_client.hello()
        if not hello.get("ok"):
            raise RuntimeError(f"broker handshake failed: {hello.get('error')}")
        hid_identity = {
            "serial": broker_record.device_id,
            "transport": "localhost-broker",
            "tcp_port": broker_record.tcp_port,
            "udp_port": broker_record.udp_port,
        }
    else:
        device_id = str(serial_number or port)
        if select_broker(discover_brokers(), device_id) is not None:
            raise RuntimeError(f"device {device_id} already has an active broker")
        owner_broker = DeviceBroker(device_id, processor)
        owner_broker.start()
        try:
            connection = serial.Serial(
                port=port, baudrate=115200, timeout=0, write_timeout=1.0
            )
            connection.dtr = True
            hid_device, hid_identity = _open_hid(serial_number)
        except (OSError, RuntimeError, serial.SerialException):
            if connection is not None:
                connection.close()
            owner_broker.stop()
            raise

    def drain_cdc() -> None:
        nonlocal transport_error, workload_completion_pending
        nonlocal prepared_native_probe_available
        if transport_error is not None or connection is None:
            return
        try:
            waiting = connection.in_waiting
            if waiting:
                chunk = connection.read(waiting)
                evidence.capture(chunk)
                for line in cdc_display.feed(chunk):
                    if line.startswith("[WORKLOAD COMPLETED]"):
                        # Native HLT evidence precedes the next HID status.
                        # Reflect completion now and refresh cycles in the loop.
                        workload.mark_idle()
                        prepared_native_probe_available = False
                        workload_completion_pending = True
                        update_console_runtime()
                    elif line.startswith(("[WORKLOAD FAULT]", "[WORKLOAD TIMEOUT]")):
                        # CDC only requests a refresh; HID supplies the actual state.
                        workload_completion_pending = True
                    if interactive and display != "quiet":
                        print_event(line)
        except (OSError, serial.SerialException) as exc:
            transport_error = f"USB CDC disconnected: {exc}"

    def exchange(request: Message) -> tuple[Message | None, float, str | None]:
        nonlocal transport_error
        began = time.monotonic()
        if broker_client is not None:
            try:
                response = broker_client.exchange(
                    request.encode(),
                    f"{os.getpid()}-{request.sequence}",
                    timeout,
                )
                latency_ms = float(response.get("latency_ms", 0.0))
                if not response.get("ok"):
                    return None, latency_ms, str(response.get("error") or "broker exchange failed")
                candidate = bytes.fromhex(str(response["reply_hex"]))
                return (
                    validate_device_reply(candidate, request, expected_processor),
                    latency_ms,
                    None,
                )
            except (OSError, RuntimeError, ValueError, KeyError) as exc:
                transport_error = f"Host broker disconnected: {exc}"
                return None, (time.monotonic() - began) * 1000.0, transport_error
        assert hid_device is not None

        def service_transport() -> str | None:
            drain_cdc()
            return transport_error

        result = exchange_hid_request(
            hid_device,
            request,
            timeout=timeout,
            expected_processor=expected_processor,
            service_transport=service_transport,
        )
        if result.error and result.error.startswith("USB HID disconnected:"):
            transport_error = result.error
        return result.reply, result.latency_ms, result.error

    def broker_snapshot() -> dict[str, Any]:
        snapshot = RuntimeStatusSnapshot(
            transport_state=_broker_runtime_state(transport_error),
            processor_mode=processor,
            request_sequence=request_sequence.value,
            observation=processor_observation,
            workload=workload,
        ).as_dict()
        snapshot.update({
            "probe_completed": probe_stats.completed,
            "probe_lost": probe_stats.lost,
            "probe_last_ms": probe_stats.last_ms,
        })
        return snapshot

    def service_broker_requests() -> None:
        nonlocal stop
        nonlocal prepared_native_probe_available
        if owner_broker is None:
            return
        for pending in owner_broker.pending():
            try:
                request = Message.decode(pending.record)
                reply, latency_ms, error = exchange(request)
                if reply is None:
                    result = {"ok": False, "error": error, "latency_ms": latency_ms}
                else:
                    if request.message_type in (
                        TYPE_WORKLOAD_BEGIN,
                        TYPE_WORKLOAD_DATA,
                        TYPE_WORKLOAD_COMMIT,
                        TYPE_WORKLOAD_CONTROL,
                    ):
                        workload.update_from_payload(reply.payload)
                        request_sequence.advance_after(request.sequence)
                        prepared_native_probe_available = (
                            workload.prepared_runtime_available
                        )
                        accept_workload_state()
                    result = {
                        "ok": True,
                        "reply_hex": reply.encode().hex(),
                        "latency_ms": latency_ms,
                    }
            except (ValueError, RuntimeError) as exc:
                result = {"ok": False, "error": str(exc), "latency_ms": 0.0}
            pending.future.set_result(result)
        while True:
            try:
                pending_control = owner_broker.controls.get_nowait()
            except queue.Empty:
                break
            try:
                bootloader_requested = False
                if pending_control.command == "status":
                    if connection is None:
                        raise RuntimeError("broker does not own a CDC connection")
                    control_evidence = send_status_request(
                        connection, pending_control.timeout
                    )
                elif pending_control.command == "bootloader":
                    if hid_device is None:
                        raise RuntimeError("broker does not own a HID connection")
                    exchange_hid_runtime_control(
                        hid_device,
                        RUNTIME_CONTROL_ENTER_BOOTLOADER,
                        request_sequence.value,
                        pending_control.timeout,
                    )
                    control_evidence = b""
                    bootloader_requested = True
                elif pending_control.command == "reboot":
                    if hid_device is None:
                        raise RuntimeError("broker does not own a HID connection")
                    exchange_hid_runtime_control(
                        hid_device,
                        RUNTIME_CONTROL_REBOOT,
                        request_sequence.value,
                        pending_control.timeout,
                    )
                    control_evidence = b""
                    bootloader_requested = True
                else:
                    raise RuntimeError(
                        f"unsupported CDC control: {pending_control.command}"
                    )
                evidence.capture(control_evidence)
                control_result = {
                    "ok": True,
                    "evidence_hex": control_evidence.hex(),
                }
            except (OSError, RuntimeError, serial.SerialException) as exc:
                bootloader_requested = False
                control_result = {"ok": False, "error": str(exc)}
            pending_control.future.set_result(control_result)
            if bootloader_requested:
                # Let the TCP handler flush the ACK before shutting down the
                # broker event loop after the expected USB disconnect.
                time.sleep(0.05)
                stop = True
        owner_broker.publish(broker_snapshot())

    def print_event(text: str) -> None:
        console.clear()
        console.write_event(text)
        if interactive:
            console.render(
                processor_observation.cpu_sequence,
                probe_stats,
                processor_observation.connected,
                command_buffer,
                command_cursor,
            )

    def update_console_runtime() -> None:
        console.set_runtime(
            workload_state=workload.lifecycle_name,
            clock_mode=workload.clock_name,
            workload_cycles=workload.cycles,
            processor_state=workload.processor_state,
        )

    def perform_workload_transaction(
        records: list[Message], description: str, *, announce: bool = True
    ) -> bool:
        result = workload_client.transact(records)
        if not result.success:
            evidence.failure(
                description, result.error or "workload transaction failed",
                failed_record=result.failed_index, record_count=result.count,
            )
            print_event(
                f"{description}: FAILED at record "
                f"{result.failed_index}/{result.count}: {result.error}"
            )
            return False
        if display == "verbose":
            for index, latency_ms in enumerate(result.latencies_ms, 1):
                print_event(
                    f"{description}: record {index}/{len(records)} accepted "
                    f"({latency_ms:.1f} ms)"
                )
        state_name = workload.lifecycle_name
        accepted = description in (
            "workload run", "workload restart", "workload stop"
        )
        outcome = "ACCEPTED" if accepted else "PASS"
        if announce:
            print_event(
                f"{description}: {outcome} ({len(records)} records)\n"
                f"  workload_id={workload.workload_id} state={state_name} "
                f"detail={workload.detail} "
                f"clock={workload.clock_name} "
                f"cycles={workload.cycles} "
                f"processor={workload.processor_state}"
            )
        return True

    def ensure_prepared_runtime_initialized() -> bool:
        """Obtain one prepared-runtime identity witness when it is available."""
        nonlocal next_due
        nonlocal processor_name
        nonlocal prepared_native_probe_available
        if (
            workload.processor_flags
            & PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED
            and processor_observation.processor is not None
        ):
            return True
        request = Message(
            TYPE_HEARTBEAT, request_sequence.value,
            heartbeat_payload(request_sequence.value),
        )
        reply, latency_ms, error = exchange(request)
        if reply is None:
            if _native_probe_unavailable(error):
                prepared_native_probe_available = False
                processor_observation.mark_disconnected()
                update_console_runtime()
                print_event(
                    "[NATIVE PROBE SUSPENDED] prepared responder did not "
                    "complete; workload control remains available"
                )
                return False
            print_event(f"processor identity probe: FAILED: {error}")
            return False
        try:
            witness = NativeServiceWitness.decode(reply.payload)
        except ValueError as exc:
            print_event(f"processor identity probe: invalid native proof: {exc}")
            return False
        request_sequence.advance_after(request.sequence)
        processor_observation.accept_witness(witness)
        processor_name = PROCESSOR_NAMES[witness.processor]
        console.set_processor(witness.processor)
        probe_stats.accept(latency_ms)
        workload.mark_prepared_runtime_initialized()
        prepared_native_probe_available = True
        next_due = time.monotonic() + interval
        print_event(
            f"processor identity probe: PASS ({processor_name}, "
            f"boot_id={witness.boot_id}, cpu_seq={witness.cpu_sequence})"
        )
        return True

    def defer_periodic_probe() -> None:
        nonlocal next_due
        next_due = time.monotonic() + interval

    def accept_workload_state() -> None:
        nonlocal prepared_native_probe_available, structured_result_reported
        nonlocal processor_name, diagnostics_observed
        evidence.observe_workload(workload)
        if workload.lifecycle not in (6, 7):
            diagnostics_observed = None
        else:
            diagnostic_key = (workload.workload_id, workload.lifecycle,
                              workload.cycles, workload.completion_reason)
            if diagnostic_key != diagnostics_observed:
                diagnostics_observed = diagnostic_key
                diagnostic, error = services.read_diagnostics(workload.workload_id)
                if diagnostic is not None:
                    if (diagnostic.lifecycle, diagnostic.cycles, diagnostic.completion_reason) == (
                            workload.lifecycle, workload.cycles, workload.completion_reason):
                        evidence.retain_diagnostics(diagnostic)
                        print_event(diagnostic.format())
                    else:
                        evidence.failure("diagnostics", "executor changed after fault status")
                else:
                    evidence.failure("diagnostics", error or "diagnostics unavailable")
                    print_event(f"diagnostics: {error}")
        prepared_native_probe_available = workload.prepared_runtime_available
        if workload.processor is not None:
            if processor_observation.processor != workload.processor:
                processor_observation.processor = workload.processor
                processor_name = PROCESSOR_NAMES[workload.processor]
                console.set_processor(workload.processor)
                print_event(
                    f"[PROCESSOR IDENTITY] {processor_name} "
                    "(firmware boot AAD16, retained identity)"
                )
        if workload.structured_result:
            result_key = (
                workload.workload_id,
                workload.lifecycle,
                workload.completion_reason,
            )
            if (workload.lifecycle in (5, 6, 7) and
                    result_key != structured_result_reported):
                print_event(
                    "WORKLOAD RESULT: " + ("PASS" if workload.passed else "FAIL")
                    + f" reason={workload.completion_reason_name}"
                    + f" cycles={workload.cycles}"
                    + f" processor_signature={workload.processor_signature:04X}"
                )
                if workload.native_output:
                    print_event(
                        f"Native result output: {workload.native_output_text}"
                    )
                structured_result_reported = result_key
        update_console_runtime()

    workload_client = WorkloadClient(
        exchange,
        request_sequence,
        workload,
        accept_workload_state,
        defer_periodic_probe,
    )

    services = RuntimeServiceClient(
        exchange, request_sequence, defer_periodic_probe
    )

    def remote_completion_entries(token: str) -> tuple[tuple[str, bool], ...]:
        """Return device-directory candidates for a Tab request."""
        slash = token.rfind("/")
        path = token[: slash + 1] if slash >= 0 else token + "/"
        entries, error = services.read_directory(path)
        if entries is None:
            print_event(f"completion: {error}")
            return ()
        return tuple((entry.name, entry.directory) for entry in entries)

    if interactive:
        print("\n[RP86 PHYSICAL PROCESSOR RUNTIME]")
        print("Host runtime shell: type help for the complete command framework.")
        print("RP2350 owns transport and lifecycle; native probes are diagnostic.\n")
        print(f"Terminal renderer: {console.renderer_name}\n")
        console.render(
            processor_observation.cpu_sequence,
            probe_stats,
            processor_observation.connected,
            command_buffer,
            command_cursor,
        )
    elif regression_workload:
        print("\n[RP86 PHYSICAL REGRESSION]")
        print(f"Workload = {regression_workload}")

    # Firmware owns boot-time identity. Every client starts with status;
    # neither attaching nor regression requires a prepared native interrupt.
    startup_status = control_record(
        "status", workload_id=0, sequence=request_sequence.value
    )
    startup_ok = perform_workload_transaction([startup_status], "attached runtime")
    if not startup_ok:
        prepared_native_probe_available = False
    identity_assertion_failed = (
        expected_processor is not None and workload.processor != expected_processor
    )
    if identity_assertion_failed:
        evidence.failure(
            "processor identity assertion",
            f"expected {expected_processor}, observed {workload.processor or 'UNPROVEN'}",
        )
        print_event(
            f"processor identity assertion: FAILED: expected {expected_processor}, "
            f"observed {workload.processor or 'UNPROVEN'}"
        )
        stop = True
    if regression_workload and (
        not startup_ok or not workload.processor_identified or identity_assertion_failed
    ):
        evidence.failure("physical regression", "startup status or firmware processor identity unavailable")
        print_event(
            "PHYSICAL REGRESSION: FAIL "
            "(startup status or firmware processor identity unavailable)"
        )
        regression_passed = False
        regression_commands.clear()
        stop = True

    posix_terminal_state = None
    if interactive and os.name != "nt" and sys.stdin.isatty():
        import termios
        import tty

        posix_terminal_state = termios.tcgetattr(sys.stdin.fileno())
        tty.setcbreak(sys.stdin.fileno())

    try:
        while not stop and (rounds == 0 or probe_stats.completed + probe_stats.lost < rounds):
            service_broker_requests()
            drain_cdc()
            if transport_error is not None:
                print_event(transport_error)
                stop = True
                continue
            if workload_completion_pending:
                workload_completion_pending = False
                completion_status = control_record(
                    "status",
                    workload_id=workload.workload_id,
                    sequence=request_sequence.value,
                )
                perform_workload_transaction(
                    [completion_status],
                    "workload completion status",
                    announce=False,
                )
            if (regression_started and not workload.completed and
                    time.monotonic() >= next_due):
                poll_status = control_record(
                    "status",
                    workload_id=workload.workload_id,
                    sequence=request_sequence.value,
                )
                perform_workload_transaction(
                    [poll_status], "workload regression status", announce=False
                )
            if regression_started and workload.completed:
                regression_passed = workload.physical_regression_passed
                if not regression_passed:
                    evidence.failure(
                        "physical regression", "; ".join(regression_failure_reasons(workload))
                    )
                print_event(
                    "PHYSICAL REGRESSION: PASS"
                    if regression_passed else
                    "PHYSICAL REGRESSION: FAIL "
                    "(invalid result, completion, or processor identity)"
                )
                stop = True
                continue
            if regression_started and workload.lifecycle in (6, 7):
                regression_passed = False
                evidence.failure("physical regression", workload.completion_reason_name)
                print_event(f"PHYSICAL REGRESSION: FAIL ({workload.completion_reason_name})")
                stop = True
                continue
            if (regression_deadline is not None and
                    time.monotonic() >= regression_deadline):
                regression_passed = False
                evidence.failure("physical regression", "regression deadline expired")
                print_event("PHYSICAL REGRESSION: FAIL (timeout)")
                stop = True
                continue
            command: str | None = None
            if regression_commands:
                command = regression_commands.pop(0)
            elif interactive:
                command_buffer, command_cursor, command, changed, tab_requested, history_delta, clear_requested = (
                    _read_terminal_command(command_buffer, command_cursor)
                )
                if clear_requested:
                    console.clear()
                    if sys.stdout.isatty():
                        sys.stdout.write("\x1b[2J\x1b[H")
                        sys.stdout.flush()
                if history_delta:
                    command_buffer = command_history.move(
                        command_buffer, history_delta
                    )
                    command_cursor = len(command_buffer)
                    changed = True
                if tab_requested:
                    before_cursor = command_buffer[:command_cursor]
                    after_cursor = command_buffer[command_cursor:]
                    token = completion_token(before_cursor)
                    remote_entries: tuple[tuple[str, bool], ...] = ()
                    if is_device_path(token):
                        remote_entries = remote_completion_entries(token)
                    completed_prefix, candidates = complete_shell_input(
                        before_cursor, remote_entries, host_cwd
                    )
                    command_buffer = completed_prefix + after_cursor
                    command_cursor = len(completed_prefix)
                    command_history.edit(command_buffer)
                    if len(candidates) > 1:
                        print_event("Completions:\n  " + "\n  ".join(candidates))
                    changed = True
                elif changed and command is None and not history_delta:
                    command_history.edit(command_buffer)
                if changed:
                    console.render(
                        processor_observation.cpu_sequence,
                        probe_stats,
                        processor_observation.connected,
                        command_buffer,
                        command_cursor,
                    )

            request_type: int | None = None
            request_payload = b""
            is_command = False
            if command is not None:
                command_history.remember(command)
                try:
                    shell_command = parse_command(command)
                except ValueError as exc:
                    print_event(str(exc))
                    shell_command = None
                if shell_command is None:
                    continue
                name = shell_command.spec.name
                arguments = shell_command.arguments
                if name == "quit":
                    stop = True
                    continue
                if name == "help":
                    try:
                        print_event(command_help(arguments[0] if arguments else None))
                    except ValueError as exc:
                        print_event(str(exc))
                elif name == "pwd":
                    if arguments:
                        print_event("Usage: pwd")
                    else:
                        print_event(str(host_cwd))
                elif name == "cd":
                    if len(arguments) > 1:
                        print_event("Usage: cd [<Host path>]")
                        continue
                    requested = arguments[0] if arguments else str(Path.home())
                    if is_device_path(requested):
                        print_event("cd: device volumes are addressed explicitly; use flash:/ or sd:/")
                        continue
                    candidate = host_list_path(requested, host_cwd)
                    try:
                        candidate = candidate.resolve(strict=True)
                    except OSError as exc:
                        print_event(f"cd: {exc}")
                        continue
                    if not candidate.is_dir():
                        print_event(f"cd: Host path is not a directory: {requested}")
                        continue
                    host_cwd = candidate
                    print_event(str(host_cwd))
                elif name in ("status", "top"):
                    record = control_record(
                        "status", workload_id=workload.workload_id,
                        sequence=request_sequence.value
                    )
                    if not perform_workload_transaction(
                        [record], "workload status"
                    ):
                        continue
                    print_event(
                        _format_runtime_top(
                            processor_name=processor_name,
                            processor_identified=(
                                processor_observation.processor is not None
                            ),
                            workload_id=workload.workload_id,
                            workload_state=workload.lifecycle,
                            workload_detail=workload.detail,
                            workload_clock_mode=workload.clock_mode,
                            workload_cycles=workload.cycles,
                            workload_processor_flags=workload.processor_flags,
                            manifest=staged_workload_manifest,
                        )
                    )
                    continue
                elif name == "info":
                    print_event(
                        "Negotiated capabilities:\n"
                        "  native probe DIAGNOSTIC / PREPARED RUNTIME ONLY\n"
                        "  console    bounded 14-byte command exchange\n"
                        "  workload   INTERNAL SRAM GENERAL EXECUTION AVAILABLE\n"
                        "  memory     INTERNAL SRAM read / write / load / save\n"
                        "  mailbox    HOST / PHYSICAL PROCESSOR SHARED\n"
                        "  filesystem RP-FLASH ls / df / cat / put\n"
                        "  storage    flash: FAT16 AVAILABLE\n"
                        "  sd         NOT AVAILABLE\n"
                        "  trace      STOPPED GENERAL-EXECUTOR SNAPSHOT"
                    )
                elif name == "timeout":
                    try:
                        timeout_ms = parse_timeout(arguments)
                    except ValueError as exc:
                        print_event(f"timeout: {exc}")
                        continue
                    setting, error = services.execution_timeout(timeout_ms)
                    if setting is None:
                        evidence.failure("execution timeout", error or "unavailable")
                        print_event(f"timeout: {error}")
                    else:
                        evidence.record({"event": "execution_timeout",
                                         "operation": "get" if timeout_ms is None else "set",
                                         **asdict(setting)})
                        print_event(setting.format())
                elif name == "trace":
                    if arguments and (arguments[0] != "save" or len(arguments) != 2):
                        print_event("usage: trace [save <host-file>]; live capture controls are unavailable")
                        continue
                    diagnostic, error = services.read_diagnostics(0)
                    if diagnostic is None:
                        evidence.failure("trace", error or "diagnostics unavailable")
                        print_event(f"trace: {error}")
                        continue
                    evidence.retain_diagnostics(diagnostic)
                    print_event(diagnostic.format())
                    if arguments:
                        try:
                            destination = host_list_path(arguments[1], host_cwd)
                            destination.write_text(
                                json.dumps(diagnostic.as_dict(), indent=2) + "\n", encoding="utf-8"
                            )
                            print_event(f"trace saved: {destination}")
                        except OSError as exc:
                            evidence.failure("trace save", str(exc))
                            print_event(f"trace save: {exc}")
                elif name == "quiet":
                    display = "quiet"
                    print_event("Runtime display: quiet (errors and command results only)")
                elif name == "verbose":
                    display = "verbose"
                    print_event("Runtime display: verbose")
                elif name == "probe":
                    request_type = TYPE_HEARTBEAT
                    request_payload = heartbeat_payload(request_sequence.value)
                    is_command = True
                elif name == "load":
                    try:
                        transfer_id = secrets.randbits(32)
                        if arguments and is_device_path(arguments[0]):
                            encoded, error = services.read_file(arguments[0])
                            if encoded is None:
                                raise ValueError(error or "device read failed")
                            manifest, image, records = workload_from_bytes(
                                encoded,
                                arguments,
                                transfer_id=transfer_id,
                                first_sequence=request_sequence.value,
                            )
                        else:
                            manifest, image, records = workload_from_command(
                                arguments,
                                transfer_id=transfer_id,
                                first_sequence=request_sequence.value,
                            )
                    except ValueError as exc:
                        evidence.failure("load", str(exc), source=arguments[0] if arguments else None)
                        print_event(f"load: {exc}")
                        if regression_workload:
                            regression_passed = False
                            stop = True
                        continue
                    print_event(
                        "Native workload upload\n"
                        f"  image   {len(image)} bytes\n"
                        f"  address 0x{manifest.load_address:05X}\n"
                        f"  entry   {manifest.entry_segment:04X}:{manifest.entry_offset:04X}\n"
                        f"  clock   {'AUTO' if not manifest.flags & 0x18 else 'FREE-RUNNING' if manifest.flags & 0x08 else 'CLOCK-STEPPED'}\n"
                        f"  CRC32   {manifest.image_crc32:08X}"
                    )
                    if _workload_upload_requires_stop(
                        workload.lifecycle,
                    ):
                        print_event("load: stopping active processor")
                        stop_record = control_record(
                            "stop",
                            workload_id=workload.workload_id,
                            sequence=request_sequence.value,
                        )
                        if not perform_workload_transaction(
                            [stop_record], "workload stop"
                        ):
                            if regression_workload:
                                regression_passed = False
                                stop = True
                            continue
                        records = upload_records(
                            manifest,
                            image,
                            transfer_id=transfer_id,
                            first_sequence=request_sequence.value,
                        )
                    if perform_workload_transaction(records, "workload upload"):
                        staged_workload_manifest = manifest
                        evidence.bind_workload(workload.workload_id, arguments[0], manifest)
                    elif regression_workload:
                        regression_passed = False
                        stop = True
                    continue
                elif name in ("run", "stop", "restart"):
                    if name in ("run", "restart") and not (
                        workload.processor_flags &
                        PROCESSOR_FLAG_PREPARED_RUNTIME_INITIALIZED
                    ):
                        if not ensure_prepared_runtime_initialized():
                            evidence.failure(name, "prepared runtime initialization failed")
                            continue
                    record = control_record(
                        name, workload_id=0, sequence=request_sequence.value
                    )
                    accepted = perform_workload_transaction(
                        [record], f"workload {name}"
                    )
                    if regression_workload and not accepted:
                        regression_passed = False
                        stop = True
                    elif regression_workload and name == "run":
                        regression_started = True
                    continue
                elif name == "console":
                    print_event(
                        "Console is active. Use: send <text>\n"
                        "Current native mailbox payload limit: 14 bytes"
                    )
                elif name == "send":
                    text_payload = " ".join(arguments)
                    if not text_payload:
                        print_event("Usage: send <text>")
                        continue
                    try:
                        request_payload = calculator_payload(arguments)
                    except ValueError:
                        request_payload = text_payload.encode("utf-8")
                    if len(request_payload) > 14:
                        print_event("Command rejected: current native mailbox consumes at most 14 bytes")
                    else:
                        request_type = TYPE_COMMAND
                        is_command = True
                elif name == "calc":
                    try:
                        request_payload = calculator_payload(arguments)
                    except ValueError as exc:
                        print_event(f"calc: {exc}")
                        continue
                    request_type = TYPE_COMMAND
                    is_command = True
                elif name == "ls":
                    if len(arguments) > 1:
                        print_event("Usage: ls [flash:/path|<Host path>]")
                        continue
                    path = arguments[0] if arguments else "flash:/"
                    if not is_device_path(path):
                        try:
                            print_event(format_host_directory(path, host_cwd))
                        except ValueError as exc:
                            print_event(f"ls: {exc}")
                        continue
                    entries, error = services.read_directory(path)
                    if entries is None:
                        print_event(f"ls: {error}")
                        continue
                    lines: list[str] = []
                    for entry in entries:
                        kind = "<DIR>" if entry.directory else f"{entry.size:>10}"
                        lines.append(f"{kind:>10}  {entry.name}")
                    print_event(
                        f"Directory of {path}\n" + ("\n".join(lines) if lines else "<empty>")
                    )
                    continue
                elif name == "df":
                    if len(arguments) > 1:
                        print_event("Usage: df [flash:]")
                        continue
                    path = arguments[0] if arguments else "flash:"
                    try:
                        request = df_request(path, request_sequence.value)
                    except ValueError as exc:
                        print_event(f"df: {exc}")
                        continue
                    reply, error = services.filesystem_request(request)
                    if reply is None:
                        print_event(f"df: {error}")
                        continue
                    try:
                        disk = parse_df(reply, request)
                    except ValueError as exc:
                        print_event(f"df: invalid device reply: {exc}")
                        continue
                    used = disk.total_kib - disk.free_kib
                    filesystem_names = {1: "FAT12", 2: "FAT16", 3: "FAT32", 4: "exFAT"}
                    print_event(
                        f"{path}  label={disk.label or '<none>'}  "
                        f"type={filesystem_names.get(disk.filesystem_type, disk.filesystem_type)}\n"
                        f"  total={disk.total_kib} KiB  used={used} KiB  "
                        f"free={disk.free_kib} KiB\n"
                        f"  cluster={disk.cluster_bytes} bytes  "
                        f"erase={disk.erase_bytes} bytes"
                    )
                    continue
                elif name == "cat":
                    if len(arguments) != 1:
                        print_event("Usage: cat <flash:/file>")
                        continue
                    path = arguments[0]
                    content, error = services.read_file(path)
                    if content is None:
                        print_event(f"cat: {error}")
                        continue
                    try:
                        rendered = content.decode("utf-8")
                    except UnicodeDecodeError:
                        rendered = content.hex(" ")
                        rendered = "Binary data (hex):\n" + rendered
                    print_event(rendered if rendered else "<empty>")
                    continue
                elif name == "put":
                    if len(arguments) != 2:
                        print_event(
                            "Usage: put <host-file> <flash:/path>\n"
                            "Example: put README.md flash:/README.TXT"
                        )
                        continue
                    host_path = host_list_path(arguments[0], host_cwd)
                    try:
                        content = host_path.read_bytes()
                        records = write_records(
                            arguments[1], content, secrets.randbits(32),
                            request_sequence.value,
                        )
                    except (OSError, ValueError) as exc:
                        print_event(
                            f"put: {exc}\n"
                            "Use an existing Host file, for example:\n"
                            "  put README.md flash:/README.TXT"
                        )
                        continue
                    failed = None
                    for index, record in enumerate(records, 1):
                        _reply, failed = services.filesystem_request(record)
                        if failed is not None:
                            print_event(
                                f"put: FAILED at record {index}/{len(records)}: "
                                f"{failed}"
                            )
                            break
                    if failed is None:
                        print_event(
                            f"put: PASS  {host_path} -> {arguments[1]}  "
                            f"({len(content)} bytes, {len(records)} records)"
                        )
                    continue
                elif name == "mem":
                    if not arguments or arguments[0] not in (
                        "read", "write", "load", "save"
                    ):
                        print_event(
                            "Usage:\n"
                            "  mem read <address> [length]\n"
                            "  mem write <address> <byte>...\n"
                            "  mem load <Host file> <address>\n"
                            "  mem save <address> <length> <Host file>"
                        )
                        continue
                    operation = arguments[0]
                    try:
                        if operation == "read" and len(arguments) in (2, 3):
                            address = int(arguments[1], 0)
                            length = int(arguments[2], 0) if len(arguments) == 3 else 16
                            if length <= 0:
                                raise ValueError("length must be positive")
                            content, error = services.read_memory(address, length)
                            print_event(
                                f"mem read: {error}" if content is None else
                                format_memory_dump(address, content)
                            )
                        elif operation == "write" and len(arguments) >= 3:
                            address = int(arguments[1], 0)
                            values = []
                            for token in arguments[2:]:
                                base = 16 if re.fullmatch(r"[0-9A-Fa-f]{2}", token) else 0
                                value = int(token, base)
                                if not 0 <= value <= 0xFF:
                                    raise ValueError(f"byte out of range: {token}")
                                values.append(value)
                            data = bytes(values)
                            error = services.write_memory(address, data)
                            print_event(
                                f"mem write: {error}" if error else
                                f"mem write: PASS  {len(data)} bytes at 0x{address:05X}"
                            )
                        elif operation == "load" and len(arguments) == 3:
                            source = host_list_path(arguments[1], host_cwd)
                            address = int(arguments[2], 0)
                            data = source.read_bytes()
                            error = services.write_memory(address, data)
                            print_event(
                                f"mem load: {error}" if error else
                                f"mem load: PASS  {source} -> 0x{address:05X} "
                                f"({len(data)} bytes)"
                            )
                        elif operation == "save" and len(arguments) == 4:
                            address = int(arguments[1], 0)
                            length = int(arguments[2], 0)
                            destination = host_list_path(arguments[3], host_cwd)
                            content, error = services.read_memory(address, length)
                            if content is None:
                                print_event(f"mem save: {error}")
                            else:
                                destination.write_bytes(content)
                                print_event(
                                    f"mem save: PASS  0x{address:05X}+{length} -> "
                                    f"{destination}"
                                )
                        else:
                            raise ValueError("arguments do not match the selected operation")
                    except (OSError, ValueError) as exc:
                        print_event(f"mem {operation}: {exc}")
                    continue
                elif name == "mailbox":
                    if not arguments:
                        print_event("Usage: mailbox <text>")
                        continue
                    request_data = " ".join(arguments).encode("utf-8")
                    if len(request_data) > MAILBOX_DATA_SIZE:
                        print_event(
                            f"mailbox: request exceeds {MAILBOX_DATA_SIZE} bytes"
                        )
                        continue
                    raw_header, error = services.read_memory(MAILBOX_BASE, 32)
                    if raw_header is None:
                        print_event(f"mailbox: {error}")
                        continue
                    try:
                        header = MailboxHeader.decode(raw_header)
                    except ValueError as exc:
                        print_event(f"mailbox: {exc}")
                        continue
                    if header.owner != 1:
                        print_event(
                            f"mailbox: busy (owner={header.owner}, "
                            f"generation={header.generation})"
                        )
                        continue
                    generation = (header.generation + 1) & 0xFFFFFFFF or 1
                    try:
                        records = mailbox_commit_records(
                            request_data, generation, request_sequence.value
                        )
                    except ValueError as exc:
                        print_event(f"mailbox: {exc}")
                        continue
                    failed = None
                    for record in records:
                        reply, failed = services.memory_request(record)
                        if reply is None:
                            break
                    if failed is not None:
                        print_event(f"mailbox: {failed}")
                        continue
                    deadline = time.monotonic() + timeout
                    result_header = None
                    while time.monotonic() < deadline:
                        raw_header, failed = services.read_memory(MAILBOX_BASE, 32)
                        if raw_header is None:
                            break
                        try:
                            candidate = MailboxHeader.decode(raw_header)
                        except ValueError as exc:
                            failed = str(exc)
                            break
                        if candidate.owner == 1 and candidate.generation == generation:
                            result_header = candidate
                            break
                        time.sleep(0.01)
                    if result_header is None:
                        print_event(f"mailbox: {failed or 'processor response timeout'}")
                        continue
                    if result_header.status != 3:
                        print_event(
                            f"mailbox: processor returned status={result_header.status}"
                        )
                        continue
                    result, failed = services.read_memory(
                        MAILBOX_BASE + 32, result_header.response_length
                    )
                    if result is None:
                        print_event(f"mailbox: {failed}")
                        continue
                    print_event(
                        f"mailbox: PASS generation={generation} "
                        f"processor={result.decode('utf-8', errors='replace')}"
                    )
                    continue
                else:
                    print_event(unavailable_message(shell_command))

            now = time.monotonic()
            if (
                request_type is None
                and now >= next_due
                and native_probe
                and prepared_native_probe_available
            ):
                request_type = TYPE_HEARTBEAT
                request_payload = heartbeat_payload(request_sequence.value)
            elif request_type is None and now >= next_due:
                # A general workload owns the physical processor and its bus.
                # Keep HID available for explicit status/stop/restart commands,
                # but do not inject the prepared-runtime diagnostic probe protocol.
                next_due = now + interval
            if request_type is None:
                time.sleep(0.02)
                continue

            request = Message(request_type, request_sequence.value, request_payload)
            reply, latency_ms, error = exchange(request)
            event = {
                "sequence": request_sequence.value,
                "request_type": request_type,
                "latency_ms": round(latency_ms, 3),
                "passed": reply is not None,
                "error": error,
            }
            evidence.record(event)
            if reply is not None:
                witness = NativeServiceWitness.decode(reply.payload)
                previous_processor = processor_observation.processor
                first_identity = previous_processor is None
                identity_changed = witness.processor != previous_processor
                if (
                    not identity_changed
                    and
                    processor_observation.boot_id == witness.boot_id
                    and processor_observation.cpu_sequence is not None
                ):
                    delta = (
                        witness.cpu_sequence - processor_observation.cpu_sequence
                    ) & 0xFFFFFFFF
                    if delta == 0 or delta >= 0x80000000:
                        error = (
                            "stale native completion counter: "
                            f"{witness.cpu_sequence} after "
                            f"{processor_observation.cpu_sequence}"
                        )
                        reply = None
                if reply is not None:
                    processor_observation.accept_witness(witness)
                    event.update(
                        {
                            "boot_id": witness.boot_id,
                            "cpu_sequence": witness.cpu_sequence,
                            "command_sequence": witness.command_sequence,
                            "native_processor": witness.processor,
                        }
                    )
                    if identity_changed:
                        processor_name = PROCESSOR_NAMES[witness.processor]
                        console.set_processor(witness.processor)
                        if first_identity:
                            identity_text = (
                                f"{processor_name} (native AAD 16) automatically identified"
                                if expected_processor is None
                                else f"{processor_name} (native AAD 16) matches Host declaration"
                            )
                        else:
                            identity_text = (
                                f"changed from {PROCESSOR_NAMES[previous_processor]} "
                                f"to {processor_name} (native AAD 16)"
                            )
                        print_event(
                            f"[PROCESSOR IDENTITY] {identity_text}"
                        )
            if reply is not None:
                if request_type == TYPE_HEARTBEAT:
                    probe_stats.accept(latency_ms)
                    processor_observation.connected = True
                if display == "verbose" or is_command:
                    if reply.message_type in (
                        TYPE_WORKLOAD_RESULT, TYPE_WORKLOAD_STATUS
                    ):
                        reply_text = "WORKLOAD REQUEST OK"
                    else:
                        reply_text = NativeServiceWitness.decode(
                            reply.payload
                        ).text.decode("ascii")
                        if (
                            processor_observation.processor == "intel-8086"
                            and reply_text.startswith("V30 ")
                        ):
                            reply_text = "8086 " + reply_text[4:]
                    print_event(
                        f"[{request_sequence.value:03d}] {reply_text}  "
                        f"latency={latency_ms:.1f} ms"
                    )
            else:
                if (
                    request_type == TYPE_HEARTBEAT
                    and _native_probe_unavailable(error)
                ):
                    prepared_native_probe_available = False
                    processor_observation.mark_disconnected()
                    event["native_probe_unavailable"] = True
                    update_console_runtime()
                    if display == "verbose" or is_command:
                        print_event(
                            "[NATIVE PROBE UNAVAILABLE] prepared responder "
                            "did not complete; workload control remains available"
                        )
                elif request_type == TYPE_HEARTBEAT:
                    probe_stats.lost += 1
                    processor_observation.mark_disconnected()
                    print_event(
                        f"[{request_sequence.value:03d}] {processor_name} NATIVE PROBE FAILED  "
                        f"latency={latency_ms:.1f} ms  error={error}"
                    )
                else:
                    print_event(
                        f"[{request_sequence.value:03d}] HOST REQUEST FAILED  "
                        f"latency={latency_ms:.1f} ms  error={error}"
                    )
            if interactive:
                console.render(
                    processor_observation.cpu_sequence,
                    probe_stats,
                    processor_observation.connected,
                    command_buffer,
                    command_cursor,
                )
            request_sequence.advance_after(request_sequence.value)
            next_due = time.monotonic() + interval
            if owner_broker is not None:
                owner_broker.publish(broker_snapshot())
    except KeyboardInterrupt:
        stop = True
    finally:
        if posix_terminal_state is not None:
            import termios

            termios.tcsetattr(
                sys.stdin.fileno(), termios.TCSADRAIN, posix_terminal_state
            )
        console.clear()
        drain_cdc()
        if owner_broker is not None:
            owner_broker.stop()
        if hid_device is not None:
            try:
                hid_device.close()
            except OSError:
                pass
        if connection is not None:
            try:
                connection.close()
            except (OSError, serial.SerialException):
                pass
        failure_reasons: list[str] = []
        if transport_error is not None:
            failure_reasons.append(transport_error)
        if identity_assertion_failed:
            failure_reasons.append("processor identity assertion failed")
        if native_probe and not (probe_stats.completed > 0 and probe_stats.lost == 0):
            failure_reasons.append("native probe acceptance failed")
        if regression_workload and regression_passed is not True:
            failure_reasons.append(
                evidence.errors[-1]["reason"] if evidence.errors else
                "physical regression did not complete successfully"
            )
        summary = {
            "schema": "rp86.runtime-session/v1",
            "started": started.isoformat(),
            "finished": datetime.now().astimezone().isoformat(),
            "workload": evidence.workload_snapshot(workload),
            "failure_reasons": failure_reasons,
            "clock_hz": 1_000_000,
            "processor": processor,
            "processor_name": processor_name,
            "boot_id": processor_observation.boot_id,
            "cpu_sequence": processor_observation.cpu_sequence,
            "command_sequence": processor_observation.command_sequence,
            "native_processor": processor_observation.processor,
            "hid_identity": hid_identity,
            "native_probe": {
                "completed": probe_stats.completed,
                "lost": probe_stats.lost,
                "latency_ms": {
                "last": probe_stats.last_ms,
                "minimum": 0.0 if not probe_stats.completed else probe_stats.minimum_ms,
                "average": probe_stats.average_ms,
                "maximum": probe_stats.maximum_ms,
                },
            },
            "transport_error": transport_error,
            "physical_regression": {
                "workload": regression_workload,
                "result_marker": workload.passed,
                "completed": workload.completed,
                "passed": regression_passed,
            } if regression_workload else None,
            "passed": not failure_reasons,
        }
        evidence.write(summary)
        print(f"RP86 runtime session closed: processor={processor_name}")
        if native_probe:
            print(
                "Native diagnostic probes: "
                f"completed={probe_stats.completed} lost={probe_stats.lost} "
                f"avg={probe_stats.average_ms:.1f} ms"
            )
        print(f"Raw CDC evidence = {evidence.raw_path}")
        print(f"Session JSON     = {evidence.json_path}")
    if transport_error is not None:
        return TRANSPORT_EXIT
    if identity_assertion_failed:
        return VALIDATION_EXIT
    if native_probe and not (probe_stats.completed > 0 and probe_stats.lost == 0):
        return VALIDATION_EXIT
    if regression_workload and regression_passed is not True:
        return VALIDATION_EXIT
    return PASS_EXIT
