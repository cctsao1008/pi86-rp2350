"""Persistent RP86 physical-processor session."""

from .common import *
from .core import *
from .transport import *
from .transport import _open_hid, _serial_module
from .exchange import *
from .console import *
from .console import _read_terminal_command
from .workload import (
    CLOCK_MODES,
    CLOCK_MODE_NAMES,
    PROCESSOR_FLAG_IDLE,
    WorkloadManifest,
)
from .calculator import calculator_payload


_WORKLOAD_STATE_NAMES = {
    0: "EMPTY",
    1: "RECEIVING",
    2: "STAGED",
    3: "RUNNING",
    4: "STOPPED",
    5: "EXITED",
    6: "FAULTED",
    7: "TIMED_OUT",
}


def _workload_state_name(state: int) -> str:
    return _WORKLOAD_STATE_NAMES.get(state, f"UNKNOWN({state})")


def _processor_execution_state(clock_mode: int, processor_flags: int) -> str:
    if processor_flags & PROCESSOR_FLAG_IDLE:
        return "IDLE / HLT"
    if clock_mode == CLOCK_MODES["stopped"]:
        return "STOPPED / RESET"
    return "ACTIVE"


def _format_runtime_top(
    *,
    processor_name: str,
    connected: bool,
    completed: int,
    lost: int,
    average_ms: float,
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

    lines = [
        "Physical processor runtime top",
        f"  {processor_name:<10} "
        f"{'ALIVE' if connected else 'NOT RESPONDING'} {processor_clock}",
        f"  Heartbeat  {completed} completed / {lost} lost",
        f"  Latency    {average_ms:.1f} ms average",
        f"  Workload   {workload_text}",
        f"  Clock mode {clock_name}",
        f"  CPU cycles {workload_cycles}",
        f"  Processor {_processor_execution_state(workload_clock_mode, workload_processor_flags)}",
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
) -> int:
    """Keep one HID/CDC session synchronized with the living physical CPU."""
    serial = _serial_module()
    output_dir.mkdir(parents=True, exist_ok=True)
    started = datetime.now().astimezone()
    timestamp = started.strftime("%Y%m%d_%H%M%S%z")
    raw_path = output_dir / f"companion_heartbeat_{timestamp}.log"
    json_path = output_dir / f"companion_heartbeat_{timestamp}.json"
    captured = bytearray()
    events: list[dict[str, Any]] = []
    stats = HeartbeatStats()
    console = ConsoleStatus(processor)
    processor_name = PROCESSOR_NAMES[processor]
    expected_processor = None if processor == "auto" else processor
    command_buffer = ""
    command_cursor = 0
    command_history = CommandHistory()
    host_cwd = Path.cwd()
    connected = True
    current_sequence = sequence & 0xFFFFFFFF
    current_boot_id: int | None = None
    current_cpu_sequence: int | None = None
    current_command_sequence: int | None = None
    current_native_processor: str | None = None
    staged_workload_id = 0
    staged_workload_state = 0
    staged_workload_detail = 0
    staged_workload_clock_mode = 0
    staged_workload_cycles = 0
    staged_workload_processor_flags = 0
    staged_workload_manifest: WorkloadManifest | None = None
    prepared_heartbeat_available = True
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
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0, write_timeout=1.0
        )
        connection.dtr = True
        hid_device, hid_identity = _open_hid(serial_number)
        device_id = str(hid_identity.get("serial") or port)
        if select_broker(discover_brokers(), device_id) is not None:
            raise RuntimeError(f"device {device_id} already has an active broker")
        owner_broker = DeviceBroker(device_id, processor)
        owner_broker.start()

    def drain_cdc() -> None:
        nonlocal transport_error
        if transport_error is not None or connection is None:
            return
        try:
            waiting = connection.in_waiting
            if waiting:
                chunk = connection.read(waiting)
                captured.extend(chunk)
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
        try:
            assert hid_device is not None
            while bytes(hid_device.read(MESSAGE_SIZE + 1)):
                pass
            record = request.encode()
            written = hid_device.write(hid_output_report(record))
            if written != MESSAGE_SIZE + 1:
                return None, 0.0, f"short HID write: {written}/{MESSAGE_SIZE + 1} bytes"
            deadline = began + timeout
            while time.monotonic() <= deadline:
                drain_cdc()
                if transport_error is not None:
                    return None, (time.monotonic() - began) * 1000.0, transport_error
                candidate = bytes(hid_device.read(MESSAGE_SIZE + 1))
                if candidate:
                    try:
                        reply = validate_device_reply(
                            candidate, request, expected_processor
                        )
                    except ValueError as exc:
                        return None, (time.monotonic() - began) * 1000.0, str(exc)
                    # Latency ends at the complete, sequence-bound HID reply.
                    # The following CDC drain preserves evidence but is not part
                    # of the physical V30 request/reply round-trip measurement.
                    latency_ms = (time.monotonic() - began) * 1000.0
                    # Firmware publishes its concise CDC proof immediately after
                    # the HID reply. Retain it without delaying the next V30 IRQ.
                    drain_deadline = time.monotonic() + 0.05
                    while time.monotonic() < drain_deadline:
                        drain_cdc()
                        if transport_error is not None:
                            break
                        time.sleep(0.001)
                    return reply, latency_ms, None
                time.sleep(0.001)
        except OSError as exc:
            transport_error = f"USB HID disconnected: {exc}"
            return None, (time.monotonic() - began) * 1000.0, transport_error
        return None, (time.monotonic() - began) * 1000.0, "heartbeat timeout"

    def broker_snapshot() -> dict[str, Any]:
        return {
            "state": "OWNER_ACTIVE" if connected else "FAULT",
            "processor": processor,
            "request_sequence": current_sequence,
            "boot_id": current_boot_id,
            "cpu_sequence": current_cpu_sequence,
            "command_sequence": current_command_sequence,
            "native_processor": current_native_processor,
            "completed": stats.completed,
            "lost": stats.lost,
            "last_ms": stats.last_ms,
        }

    def service_broker_requests() -> None:
        nonlocal stop
        if owner_broker is None:
            return
        for pending in owner_broker.pending():
            try:
                request = Message.decode(pending.record)
                reply, latency_ms, error = exchange(request)
                if reply is None:
                    result = {"ok": False, "error": error, "latency_ms": latency_ms}
                else:
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
                    evidence = send_status_request(
                        connection, pending_control.timeout
                    )
                elif pending_control.command == "bootloader":
                    if hid_device is None:
                        raise RuntimeError("broker does not own a HID connection")
                    exchange_hid_runtime_control(
                        hid_device,
                        RUNTIME_CONTROL_ENTER_BOOTLOADER,
                        current_sequence,
                        pending_control.timeout,
                    )
                    evidence = b""
                    bootloader_requested = True
                elif pending_control.command == "reboot":
                    if hid_device is None:
                        raise RuntimeError("broker does not own a HID connection")
                    exchange_hid_runtime_control(
                        hid_device,
                        RUNTIME_CONTROL_REBOOT,
                        current_sequence,
                        pending_control.timeout,
                    )
                    evidence = b""
                    bootloader_requested = True
                else:
                    raise RuntimeError(
                        f"unsupported CDC control: {pending_control.command}"
                    )
                captured.extend(evidence)
                control_result = {
                    "ok": True,
                    "evidence_hex": evidence.hex(),
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
        print(text)
        if interactive:
            console.render(
                current_cpu_sequence, stats, connected, command_buffer, command_cursor
            )

    def perform_workload_transaction(
        records: list[Message], description: str
    ) -> bool:
        nonlocal current_sequence, next_due
        nonlocal staged_workload_id, staged_workload_state, staged_workload_detail
        nonlocal staged_workload_clock_mode, staged_workload_cycles
        nonlocal staged_workload_processor_flags
        nonlocal prepared_heartbeat_available
        for index, request in enumerate(records, 1):
            reply, latency_ms, error = exchange(request)
            if reply is None:
                print_event(
                    f"{description}: FAILED at record {index}/{len(records)}: {error}"
                )
                return False
            current_sequence = (request.sequence + 1) & 0xFFFFFFFF
            if current_sequence == 0:
                current_sequence = 1
            try:
                (staged_workload_id, staged_workload_state,
                 staged_workload_detail, staged_workload_clock_mode,
                 staged_workload_cycles,
                 staged_workload_processor_flags) = decode_status_payload(
                     reply.payload
                 )
                if (
                    staged_workload_state == 3
                    and staged_workload_clock_mode == 2
                ):
                    prepared_heartbeat_available = False
            except ValueError as exc:
                print_event(f"{description}: invalid status payload: {exc}")
                return False
            if display == "verbose":
                print_event(
                    f"{description}: record {index}/{len(records)} accepted "
                    f"({latency_ms:.1f} ms)"
                )
        next_due = time.monotonic() + interval
        state_name = _workload_state_name(staged_workload_state)
        print_event(
            f"{description}: PASS ({len(records)} records)\n"
            f"  workload_id={staged_workload_id} state={state_name} "
            f"detail={staged_workload_detail} "
            f"clock={CLOCK_MODE_NAMES.get(staged_workload_clock_mode, staged_workload_clock_mode)} "
            f"cycles={staged_workload_cycles} "
            f"processor={_processor_execution_state(staged_workload_clock_mode, staged_workload_processor_flags)}"
        )
        return True

    def perform_filesystem_request(
        request: Message,
    ) -> tuple[Message | None, str | None]:
        nonlocal current_sequence, next_due
        reply, _latency_ms, error = exchange(request)
        current_sequence = (request.sequence + 1) & 0xFFFFFFFF
        if current_sequence == 0:
            current_sequence = 1
        next_due = time.monotonic() + interval
        if reply is None:
            return None, error or "filesystem exchange failed"
        try:
            validate_filesystem_payload(reply, request)
        except ValueError as exc:
            return None, str(exc)
        return reply, None

    def perform_memory_request(
        request: Message,
    ) -> tuple[Message | None, str | None]:
        nonlocal current_sequence, next_due
        reply, _latency_ms, error = exchange(request)
        current_sequence = (request.sequence + 1) & 0xFFFFFFFF or 1
        next_due = time.monotonic() + interval
        if reply is None:
            return None, error or "memory exchange failed"
        try:
            validate_memory_reply(reply, request)
        except ValueError as exc:
            return None, str(exc)
        return reply, None

    def read_memory(address: int, length: int) -> tuple[bytes | None, str | None]:
        content = bytearray()
        while len(content) < length:
            chunk_length = min(40, length - len(content))
            try:
                request = memory_read_request(
                    address + len(content), chunk_length, current_sequence
                )
            except ValueError as exc:
                return None, str(exc)
            reply, error = perform_memory_request(request)
            if reply is None:
                return None, error
            try:
                content.extend(parse_memory_read(reply, request))
            except ValueError as exc:
                return None, str(exc)
        return bytes(content), None

    def write_memory(address: int, data: bytes) -> str | None:
        try:
            records = memory_write_records(address, data, current_sequence)
        except ValueError as exc:
            return str(exc)
        for request in records:
            reply, error = perform_memory_request(request)
            if reply is None:
                return error
        return None

    def read_device_directory(
        path: str,
    ) -> tuple[list[Any] | None, str | None]:
        """Read one RP2350 filesystem directory without formatting it."""
        cursor = 0
        entries: list[Any] = []
        while True:
            try:
                request = list_request(path, cursor, current_sequence)
            except ValueError as exc:
                return None, str(exc)
            reply, error = perform_filesystem_request(request)
            if reply is None:
                return None, error
            try:
                entry = parse_list(reply, request)
            except ValueError as exc:
                return None, f"invalid device reply: {exc}"
            if entry.eof:
                return entries, None
            entries.append(entry)
            cursor = entry.next_cursor

    def remote_completion_entries(token: str) -> tuple[tuple[str, bool], ...]:
        """Return device-directory candidates for a Tab request."""
        slash = token.rfind("/")
        path = token[: slash + 1] if slash >= 0 else token + "/"
        entries, error = read_device_directory(path)
        if entries is None:
            print_event(f"completion: {error}")
            return ()
        return tuple((entry.name, entry.directory) for entry in entries)

    if interactive:
        print(f"\n[{processor_name} INTERACTIVE HEARTBEAT]")
        print("Host runtime shell: type help for the complete command framework.")
        print("Heartbeat runs in the background; command traffic has priority.\n")
        console.render(current_cpu_sequence, stats, connected, command_buffer, command_cursor)

    posix_terminal_state = None
    if interactive and os.name != "nt" and sys.stdin.isatty():
        import termios
        import tty

        posix_terminal_state = termios.tcgetattr(sys.stdin.fileno())
        tty.setcbreak(sys.stdin.fileno())

    try:
        while not stop and (rounds == 0 or stats.completed + stats.lost < rounds):
            service_broker_requests()
            drain_cdc()
            if transport_error is not None:
                print_event(transport_error)
                stop = True
                continue
            command: str | None = None
            if interactive:
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
                        current_cpu_sequence, stats, connected, command_buffer
                        , command_cursor
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
                    print_event(
                        f"{processor_name} ALIVE={connected} completed={stats.completed} "
                        f"lost={stats.lost} min/avg/max="
                        f"{stats.minimum_ms if stats.completed else 0:.1f}/"
                        f"{stats.average_ms:.1f}/{stats.maximum_ms:.1f} ms\n"
                        f"boot_id={current_boot_id if current_boot_id is not None else '--'} "
                        f"cpu_seq={current_cpu_sequence if current_cpu_sequence is not None else '--'} "
                        f"command_seq={current_command_sequence if current_command_sequence is not None else '--'}"
                    )
                    if name == "top":
                        print_event(
                            _format_runtime_top(
                                processor_name=processor_name,
                                connected=connected,
                                completed=stats.completed,
                                lost=stats.lost,
                                average_ms=stats.average_ms,
                                workload_id=staged_workload_id,
                                workload_state=staged_workload_state,
                                workload_detail=staged_workload_detail,
                                workload_clock_mode=staged_workload_clock_mode,
                                workload_cycles=staged_workload_cycles,
                                workload_processor_flags=staged_workload_processor_flags,
                                manifest=staged_workload_manifest,
                            )
                        )
                    record = control_record(
                        "status", workload_id=staged_workload_id,
                        sequence=current_sequence
                    )
                    perform_workload_transaction([record], "workload status")
                    continue
                elif name == "info":
                    print_event(
                        "Negotiated capabilities:\n"
                        "  heartbeat  AVAILABLE\n"
                        "  console    bounded 14-byte command exchange\n"
                        "  workload   INTERNAL SRAM GENERAL EXECUTION AVAILABLE\n"
                        "  memory     INTERNAL SRAM read / write / load / save\n"
                        "  mailbox    HOST / PHYSICAL PROCESSOR SHARED\n"
                        "  filesystem RP-FLASH ls / df / cat / put\n"
                        "  storage    flash: FAT16 AVAILABLE\n"
                        "  sd         NOT AVAILABLE\n"
                        "  trace      NOT AVAILABLE"
                    )
                elif name == "quiet":
                    display = "quiet"
                    print_event("Heartbeat display: quiet (errors and commands only)")
                elif name == "verbose":
                    display = "verbose"
                    print_event("Heartbeat display: verbose")
                elif name == "ping":
                    request_type = TYPE_HEARTBEAT
                    request_payload = heartbeat_payload(current_sequence)
                    is_command = True
                elif name == "load":
                    try:
                        transfer_id = secrets.randbits(32)
                        manifest, image, records = workload_from_command(
                            arguments,
                            transfer_id=transfer_id,
                            first_sequence=current_sequence,
                        )
                    except ValueError as exc:
                        print_event(f"load: {exc}")
                        continue
                    print_event(
                        "Native workload upload\n"
                        f"  image   {len(image)} bytes\n"
                        f"  address 0x{manifest.load_address:05X}\n"
                        f"  entry   {manifest.entry_segment:04X}:{manifest.entry_offset:04X}\n"
                        f"  clock   {'AUTO' if not manifest.flags & 0x18 else 'FREE-RUNNING' if manifest.flags & 0x08 else 'CLOCK-STEPPED'}\n"
                        f"  CRC32   {manifest.image_crc32:08X}"
                    )
                    if perform_workload_transaction(records, "workload upload"):
                        staged_workload_manifest = manifest
                    continue
                elif name in ("run", "stop", "restart"):
                    record = control_record(
                        name, workload_id=0, sequence=current_sequence
                    )
                    perform_workload_transaction([record], f"workload {name}")
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
                    entries, error = read_device_directory(path)
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
                        request = df_request(path, current_sequence)
                    except ValueError as exc:
                        print_event(f"df: {exc}")
                        continue
                    reply, error = perform_filesystem_request(request)
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
                    offset = 0
                    content = bytearray()
                    while True:
                        try:
                            request = read_request(path, offset, current_sequence)
                        except ValueError as exc:
                            print_event(f"cat: {exc}")
                            break
                        reply, error = perform_filesystem_request(request)
                        if reply is None:
                            print_event(f"cat: {error}")
                            break
                        try:
                            chunk = parse_read(reply, request)
                        except ValueError as exc:
                            print_event(f"cat: invalid device reply: {exc}")
                            break
                        if chunk.offset != offset:
                            print_event(
                                f"cat: reply offset mismatch {chunk.offset} != {offset}"
                            )
                            break
                        content.extend(chunk.data)
                        offset += len(chunk.data)
                        if chunk.eof:
                            try:
                                rendered = content.decode("utf-8")
                            except UnicodeDecodeError:
                                rendered = content.hex(" ")
                                rendered = "Binary data (hex):\n" + rendered
                            print_event(rendered if rendered else "<empty>")
                            break
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
                            current_sequence,
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
                        _reply, failed = perform_filesystem_request(record)
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
                            content, error = read_memory(address, length)
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
                            error = write_memory(address, data)
                            print_event(
                                f"mem write: {error}" if error else
                                f"mem write: PASS  {len(data)} bytes at 0x{address:05X}"
                            )
                        elif operation == "load" and len(arguments) == 3:
                            source = host_list_path(arguments[1], host_cwd)
                            address = int(arguments[2], 0)
                            data = source.read_bytes()
                            error = write_memory(address, data)
                            print_event(
                                f"mem load: {error}" if error else
                                f"mem load: PASS  {source} -> 0x{address:05X} "
                                f"({len(data)} bytes)"
                            )
                        elif operation == "save" and len(arguments) == 4:
                            address = int(arguments[1], 0)
                            length = int(arguments[2], 0)
                            destination = host_list_path(arguments[3], host_cwd)
                            content, error = read_memory(address, length)
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
                    raw_header, error = read_memory(MAILBOX_BASE, 32)
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
                            request_data, generation, current_sequence
                        )
                    except ValueError as exc:
                        print_event(f"mailbox: {exc}")
                        continue
                    failed = None
                    for record in records:
                        reply, failed = perform_memory_request(record)
                        if reply is None:
                            break
                    if failed is not None:
                        print_event(f"mailbox: {failed}")
                        continue
                    deadline = time.monotonic() + timeout
                    result_header = None
                    while time.monotonic() < deadline:
                        raw_header, failed = read_memory(MAILBOX_BASE, 32)
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
                    result, failed = read_memory(
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
                and prepared_heartbeat_available
            ):
                request_type = TYPE_HEARTBEAT
                request_payload = heartbeat_payload(current_sequence)
            elif request_type is None and now >= next_due:
                # A general workload owns the physical processor and its bus.
                # Keep HID available for explicit status/stop/restart commands,
                # but do not inject the prepared-runtime heartbeat protocol.
                next_due = now + interval
            if request_type is None:
                time.sleep(0.02)
                continue

            request = Message(request_type, current_sequence, request_payload)
            reply, latency_ms, error = exchange(request)
            event = {
                "sequence": current_sequence,
                "request_type": request_type,
                "latency_ms": round(latency_ms, 3),
                "passed": reply is not None,
                "error": error,
            }
            events.append(event)
            if reply is not None:
                witness = NativeServiceWitness.decode(reply.payload)
                first_identity = current_native_processor is None
                identity_changed = witness.processor != current_native_processor
                if (
                    not identity_changed
                    and
                    current_boot_id == witness.boot_id
                    and current_cpu_sequence is not None
                ):
                    delta = (
                        witness.cpu_sequence - current_cpu_sequence
                    ) & 0xFFFFFFFF
                    if delta == 0 or delta >= 0x80000000:
                        error = (
                            "stale native completion counter: "
                            f"{witness.cpu_sequence} after {current_cpu_sequence}"
                        )
                        reply = None
                if reply is not None:
                    current_boot_id = witness.boot_id
                    current_cpu_sequence = witness.cpu_sequence
                    current_command_sequence = witness.command_sequence
                    current_native_processor = witness.processor
                    event.update(
                        {
                            "boot_id": witness.boot_id,
                            "cpu_sequence": witness.cpu_sequence,
                            "command_sequence": witness.command_sequence,
                            "native_processor": witness.processor,
                        }
                    )
                    if identity_changed:
                        previous_processor = current_native_processor
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
                stats.accept(latency_ms)
                connected = True
                if display == "verbose" or is_command:
                    if reply.message_type in (
                        TYPE_WORKLOAD_RESULT, TYPE_WORKLOAD_STATUS
                    ):
                        reply_text = "WORKLOAD REQUEST OK"
                    else:
                        reply_text = NativeServiceWitness.decode(
                            reply.payload
                        ).text.decode("ascii")
                        if current_native_processor == "intel-8086" and reply_text.startswith("V30 "):
                            reply_text = "8086 " + reply_text[4:]
                    print_event(
                        f"[{current_sequence:03d}] {reply_text}  "
                        f"latency={latency_ms:.1f} ms"
                    )
            else:
                stats.lost += 1
                connected = False
                print_event(
                    f"[{current_sequence:03d}] {processor_name} HEARTBEAT LOST  "
                    f"latency={latency_ms:.1f} ms  error={error}"
                )
            if interactive:
                console.render(
                    current_cpu_sequence, stats, connected, command_buffer, command_cursor
                )
            current_sequence = (current_sequence + 1) & 0xFFFFFFFF
            if current_sequence == 0:
                current_sequence = 1
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
        raw_path.write_bytes(captured)
        summary = {
            "schema": "rp86.runtime-session/v1",
            "started": started.isoformat(),
            "clock_hz": 1_000_000,
            "processor": processor,
            "processor_name": processor_name,
            "boot_id": current_boot_id,
            "cpu_sequence": current_cpu_sequence,
            "command_sequence": current_command_sequence,
            "native_processor": current_native_processor,
            "hid_identity": hid_identity,
            "completed": stats.completed,
            "lost": stats.lost,
            "latency_ms": {
                "last": stats.last_ms,
                "minimum": 0.0 if not stats.completed else stats.minimum_ms,
                "average": stats.average_ms,
                "maximum": stats.maximum_ms,
            },
            "events": events,
            "transport_error": transport_error,
            "raw_cdc_log": str(raw_path.resolve()),
            "passed": stats.completed > 0 and stats.lost == 0 and
            transport_error is None,
        }
        json_path.write_text(
            json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(
            f"{processor_name} heartbeat stopped: completed={stats.completed} lost={stats.lost} "
            f"avg={stats.average_ms:.1f} ms"
        )
        print(f"Raw CDC evidence = {raw_path}")
        print(f"Session JSON     = {json_path}")
    if transport_error is not None:
        return TRANSPORT_EXIT
    return PASS_EXIT if stats.completed > 0 and stats.lost == 0 else VALIDATION_EXIT
