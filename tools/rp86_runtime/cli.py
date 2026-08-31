"""Command-line interface for the RP86 Host runtime."""

from .common import *
from .core import *
from .transport import *
from .exchange import *
from .session import *

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="exchange fixed 64-byte records with a physical 8086-class processor"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--simulate", action="store_true")
    mode.add_argument("--exchange", action="store_true")
    mode.add_argument("--interactive", action="store_true")
    mode.add_argument(
        "--physical-regression", metavar="P86W",
        help="load, run, and verify one physical workload to COMPLETED",
    )
    mode.add_argument(
        "--bootloader", action="store_true",
        help="request RP2350 UF2 bootloader mode over HID, with CDC fallback",
    )
    mode.add_argument(
        "--reboot", action="store_true",
        help="restart the canonical RP2350 firmware over HID, with CDC fallback",
    )
    mode.add_argument(
        "--status", action="store_true",
        help="request canonical RP2350 runtime status over USB CDC",
    )
    mode.add_argument("--list-devices", action="store_true")
    parser.add_argument(
        "--port",
        help="composite CDC port, for example COM14 (default: auto-detect)",
    )
    parser.add_argument("--sequence", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--hid-serial")
    parser.add_argument(
        "--processor",
        choices=tuple(PROCESSOR_NAMES),
        default="auto",
        help=(
            "optional strict processor assertion; default auto uses the "
            "physical AAD 16 identity witness"
        ),
    )
    parser.add_argument("--output-dir", type=Path, default=default_output_dir())
    parser.add_argument(
        "--monitor", action="store_true",
        help="keep a headless Host runtime owner active without probing the workload",
    )
    parser.add_argument(
        "--native-probe", action="store_true",
        help="periodically run the prepared diagnostic liveness probe",
    )
    parser.add_argument(
        "--attach", action="store_true",
        help="attach to an already-running RP86 processor runtime without RESET evidence",
    )
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--probe-timeout", type=float, default=2.0)
    parser.add_argument("--rounds", type=int, default=0, help="0 means run until Ctrl+C")
    parser.add_argument(
        "--display",
        choices=("quiet", "status", "live", "plain", "verbose"),
        default="live",
        help=(
            "terminal renderer: status/live uses an updating status row; "
            "plain is redirect-safe; quiet suppresses routine native output"
        ),
    )
    parser.add_argument("--json", action="store_true", help="print only stable JSON")
    return parser


def _regression_workload_error(workload: str | None) -> str | None:
    if workload is None or is_device_path(workload) or Path(workload).is_file():
        return None
    return (
        f"workload not found: {workload}. "
        "A Windows checkout does not contain WSL build artifacts; use the "
        r"\\wsl.localhost\<distro>\...\build\workloads\<name>.P86W path"
    )


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    regression_error = _regression_workload_error(args.physical_regression)
    if regression_error is not None:
        print(f"ERROR: {regression_error}", file=sys.stderr)
        return VALIDATION_EXIT
    if args.simulate:
        request = Message(TYPE_HELLO, args.sequence, CANONICAL_GREETING)
        response = Message.decode(simulate_v30(request.encode()))
        if args.json:
            print(json.dumps({
                "schema": "rp86.host-protocol-simulation/v1",
                "request": request.payload.decode("ascii"),
                "reply": response.payload.decode("ascii"),
                "sequence": response.sequence,
                "passed": True,
            }, separators=(",", ":")))
        else:
            print(f"OpenAI Codex > {request.payload.decode('ascii')}")
            print(f"NEC V30      > {response.payload.decode('ascii')}")
            print("Protocol simulation: PASS")
        return PASS_EXIT

    if args.list_devices:
        try:
            devices = list_hid_devices()
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return DEPENDENCY_EXIT
        if args.json:
            public = [{k: v for k, v in item.items() if k != "_native_path"} for item in devices]
            print(json.dumps(public, indent=2, ensure_ascii=False))
        elif not devices:
            print(f"No VID {USB_VID:04X} PID {USB_PID:04X} HID interface found.")
        else:
            for item in devices:
                print(
                    f"VID {item['vid']} PID {item['pid']}  "
                    f"serial={item['serial'] or '<none>'}  {item['product']}"
                )
        return PASS_EXIT if devices else TRANSPORT_EXIT

    if args.interval <= 0:
        parser.error("--interval must be greater than zero")
    if args.probe_timeout <= 0:
        parser.error("--probe-timeout must be greater than zero")
    if args.rounds < 0:
        parser.error("--rounds cannot be negative")
    if args.json and (
        args.interactive or args.monitor or args.native_probe or
        args.physical_regression
    ):
        parser.error("persistent runtime monitoring cannot be combined with --json")
    if args.attach and not (args.interactive or args.monitor or args.native_probe):
        parser.error("--attach requires --interactive, --monitor, or --native-probe")

    broker_record: BrokerRecord | None = None
    if args.status or args.bootloader or args.reboot or (
        args.attach and (args.interactive or args.monitor or args.native_probe)
    ) or args.physical_regression:
        device_hint = args.hid_serial
        if device_hint is None and args.port:
            try:
                device_hint = cdc_serial_for_port(args.port)
            except RuntimeError:
                device_hint = None
        try:
            broker_record = select_broker(discover_brokers(), device_hint)
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT

    if args.status and broker_record is not None:
        try:
            broker_client = BrokerClient(
                broker_record, f"status-{os.getpid()}"
            )
            reply = broker_client.hello()
            control = broker_client.control(
                "status", f"status-{os.getpid()}", args.timeout
            )
        except (OSError, RuntimeError, ValueError) as exc:
            print(f"ERROR: Host broker status failed: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT
        if not control.get("ok"):
            print(
                f"ERROR: Host broker status failed: {control.get('error')}",
                file=sys.stderr,
            )
            return TRANSPORT_EXIT
        snapshot = reply.get("snapshot", {})
        print("\n[HOST BROKER STATUS]")
        print(f"Device ID                  = {broker_record.device_id}")
        processor_status = (
            snapshot.get("native_processor")
            or reply.get("processor", "UNKNOWN")
        )
        print(f"Physical processor         = {str(processor_status).upper()}")
        print(f"Broker state               = {snapshot.get('state', 'UNKNOWN')}")
        print(f"TCP / UDP                  = {broker_record.tcp_port} / {broker_record.udp_port}")
        print(
            "Native probes completed/lost = "
            f"{snapshot.get('probe_completed', 0)} / "
            f"{snapshot.get('probe_lost', 0)}"
        )
        print("Hardware owner              = EXISTING BROKER")
        print("HOST BROKER STATUS RESULT = PASS")
        print_status_evidence(bytes.fromhex(str(control["evidence_hex"])))
        return PASS_EXIT

    if args.bootloader and broker_record is not None:
        try:
            control = BrokerClient(
                broker_record, f"bootloader-{os.getpid()}"
            ).control("bootloader", f"bootloader-{os.getpid()}", args.timeout)
        except (OSError, RuntimeError, ValueError) as exc:
            print(f"ERROR: Host broker bootloader failed: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT
        if not control.get("ok"):
            print(
                f"ERROR: Host broker bootloader failed: {control.get('error')}",
                file=sys.stderr,
            )
            return TRANSPORT_EXIT
        evidence = bytes.fromhex(str(control["evidence_hex"]))
        print("RP2350 bootloader request = ACKNOWLEDGED VIA HOST BROKER")
        print("RP2350 UF2 bootloader     = ENTERING")
        print(f"CDC evidence bytes        = {len(evidence)}")
        return PASS_EXIT

    if args.reboot and broker_record is not None:
        try:
            control = BrokerClient(
                broker_record, f"reboot-{os.getpid()}"
            ).control("reboot", f"reboot-{os.getpid()}", args.timeout)
        except (OSError, RuntimeError, ValueError) as exc:
            print(f"ERROR: Host broker reboot failed: {exc}", file=sys.stderr)
            return TRANSPORT_EXIT
        if not control.get("ok"):
            print(
                f"ERROR: Host broker reboot failed: {control.get('error')}",
                file=sys.stderr,
            )
            return TRANSPORT_EXIT
        print("RP2350 reboot request     = ACKNOWLEDGED VIA HOST BROKER")
        print("RP2350 canonical runtime  = RESTARTING")
        return PASS_EXIT

    if broker_record is not None:
        session_processor = (
            args.processor if args.processor != "auto" else "auto"
        )
        identity_policy = (
            "native identity auto-detect"
            if session_processor == "auto"
            else f"strict {session_processor} assertion"
        )
        if not args.json:
            print(
                "Connected to existing Host broker = "
                f"{broker_record.device_id} (TCP {broker_record.tcp_port}, "
                f"{identity_policy})"
            )
        return persistent_monitor(
            port="",
            sequence=args.sequence or 1,
            timeout=args.probe_timeout,
            interval=args.interval,
            output_dir=args.output_dir,
            serial_number=broker_record.device_id,
            display=args.display,
            interactive=args.interactive,
            rounds=args.rounds,
            processor=session_processor,
            broker_record=broker_record,
            native_probe=args.native_probe,
            regression_workload=args.physical_regression,
            regression_timeout=args.timeout,
        )

    if args.bootloader or args.reboot:
        operation = (
            RUNTIME_CONTROL_ENTER_BOOTLOADER
            if args.bootloader else RUNTIME_CONTROL_REBOOT
        )
        device_hint = args.hid_serial
        if device_hint is None and args.port:
            try:
                device_hint = cdc_serial_for_port(args.port)
            except RuntimeError:
                pass
        try:
            identity = send_hid_runtime_control(
                operation, args.sequence, args.timeout, device_hint
            )
            print(
                "RP2350 bootloader request = ACKNOWLEDGED VIA HID"
                if args.bootloader else
                "RP2350 reboot request     = ACKNOWLEDGED VIA HID"
            )
            print(
                "RP2350 UF2 bootloader     = ENTERING"
                if args.bootloader else
                "RP2350 canonical runtime  = RESTARTING"
            )
            print(f"USB device serial         = {identity.get('serial') or '<none>'}")
            return PASS_EXIT
        except RuntimeError as hid_error:
            if not args.json:
                print(f"HID control unavailable; trying CDC fallback: {hid_error}")

    try:
        args.port, auto_port = resolve_cdc_port(args.port, args.hid_serial)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return TRANSPORT_EXIT
    if auto_port and not args.json:
        print(f"Auto-selected CDC port = {args.port}")
    if args.hid_serial is None:
        try:
            args.hid_serial = cdc_serial_for_port(args.port)
        except RuntimeError:
            args.hid_serial = None
    if args.status:
        return request_status(args.port, args.timeout)
    if args.bootloader:
        return request_bootloader(args.port, args.timeout)
    if args.reboot:
        return request_reboot_cdc(args.port, args.timeout)
    if args.physical_regression:
        try:
            return persistent_monitor(
                port=args.port,
                sequence=args.sequence or 1,
                timeout=args.probe_timeout,
                interval=args.interval,
                output_dir=args.output_dir,
                serial_number=args.hid_serial,
                display=args.display,
                interactive=False,
                rounds=0,
                processor=args.processor,
                native_probe=False,
                regression_workload=args.physical_regression,
                regression_timeout=args.timeout,
            )
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return DEPENDENCY_EXIT
    if args.attach:
        try:
            return persistent_monitor(
                port=args.port,
                sequence=args.sequence or 1,
                timeout=args.probe_timeout,
                interval=args.interval,
                output_dir=args.output_dir,
                serial_number=args.hid_serial,
                display=args.display,
                interactive=args.interactive,
                rounds=args.rounds,
                processor=args.processor,
                native_probe=args.native_probe,
            )
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return DEPENDENCY_EXIT
    try:
        result, exit_code = physical_exchange(
            port=args.port,
            sequence=args.sequence,
            timeout=args.timeout,
            output_dir=args.output_dir,
            processor=args.processor,
            serial_number=args.hid_serial,
            echo_cdc=not args.json and not args.interactive,
        )
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return DEPENDENCY_EXIT
    if args.json:
        print(json.dumps(result, separators=(",", ":"), ensure_ascii=False))
    else:
        print_human_result(result)
    if exit_code != PASS_EXIT:
        return exit_code
    if args.interactive or args.monitor or args.native_probe:
        return persistent_monitor(
            port=args.port,
            sequence=(args.sequence + 1) & 0xFFFFFFFF or 1,
            timeout=args.probe_timeout,
            interval=args.interval,
            output_dir=args.output_dir,
            serial_number=args.hid_serial,
            display=args.display,
            interactive=args.interactive,
            rounds=args.rounds,
            processor=args.processor,
            native_probe=args.native_probe,
        )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
