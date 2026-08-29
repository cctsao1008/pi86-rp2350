"""One-shot physical exchange and evidence presentation."""

from .common import *
from .core import *
from .transport import *
from .transport import _open_hid, _serial_module

def physical_exchange(
    port: str,
    sequence: int,
    timeout: float,
    output_dir: Path,
    processor: str = "auto",
    serial_number: str | None = None,
    echo_cdc: bool = True,
) -> tuple[dict[str, Any], int]:
    """Perform one HID round trip while retaining the CDC physical evidence."""
    serial = _serial_module()
    request = Message(TYPE_HELLO, sequence, CANONICAL_GREETING)
    request_record = request.encode()
    captured = bytearray()
    hid_reply_raw: bytes | None = None
    hid_device = None
    connection = None
    hid_identity: dict[str, Any] = {}
    transport_errors: list[str] = []
    started = datetime.now().astimezone()

    try:
        connection = serial.Serial(
            port=port, baudrate=115200, timeout=0, write_timeout=1.0
        )
        connection.dtr = True
        time.sleep(0.15)
        connection.reset_input_buffer()

        hid_device, hid_identity = _open_hid(serial_number)
        written = hid_device.write(hid_output_report(request_record))
        if written != MESSAGE_SIZE + 1:
            raise RuntimeError(f"short HID write: {written}/{MESSAGE_SIZE + 1} bytes")

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            waiting = connection.in_waiting
            if waiting:
                chunk = connection.read(waiting)
                captured.extend(chunk)
                if echo_cdc:
                    sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                    sys.stdout.flush()

            if hid_reply_raw is None:
                candidate = bytes(hid_device.read(MESSAGE_SIZE + 1))
                if candidate:
                    hid_reply_raw = normalize_hid_input(candidate)

            if any(marker in captured for marker in TERMINAL_MARKERS) and hid_reply_raw is not None:
                break
            time.sleep(0.005)
        else:
            transport_errors.append(f"exchange timed out after {timeout:.1f} seconds")
    except (OSError, RuntimeError, ValueError) as exc:
        transport_errors.append(str(exc))
    finally:
        if hid_device is not None:
            hid_device.close()
        if connection is not None:
            connection.close()

    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = started.strftime("%Y%m%d_%H%M%S%z")
    text = captured.decode("utf-8", errors="replace")
    profile = RP86_RUNTIME
    raw_path = output_dir / f"{profile.filename_prefix}_{timestamp}.log"
    raw_path.write_bytes(captured)
    cdc_report = validate_output(text, profile)
    story = list(explain_output(text, cdc_report))

    cdc_sequence = None

    reply: Message | None = None
    if hid_reply_raw is None:
        transport_errors.append("no complete 64-byte HID reply was received")
    else:
        try:
            reply = validate_live_reply(
                hid_reply_raw,
                request,
                None if processor == "auto" else processor,
            )
        except ValueError as exc:
            transport_errors.append(str(exc))

    hid_pass = reply is not None
    cdc_pass = cdc_report.passed
    overall_pass = hid_pass and cdc_pass and not transport_errors
    reply_json: dict[str, Any] | None = None
    if reply is not None:
        reply_text = reply.payload
        native_witness: NativeServiceWitness | None = None
        native_witness = NativeServiceWitness.decode(reply.payload)
        reply_text = native_witness.text
        reply_json = {
            "transport": "USB HID",
            "bytes": MESSAGE_SIZE,
            "type": reply.message_type,
            "sequence": reply.sequence,
            "payload": reply_text.decode("ascii"),
            "sha256": hashlib.sha256(hid_reply_raw or b"").hexdigest(),
        }
        if native_witness is not None:
            reply_json["native_witness"] = {
                "boot_id": native_witness.boot_id,
                "cpu_sequence": native_witness.cpu_sequence,
                "command_sequence": native_witness.command_sequence,
                "service_type": native_witness.service_type,
                "processor": native_witness.processor,
                "identity_source": "physical AAD 16 discriminator",
            }
    detected_processor = native_witness.processor if reply is not None else processor
    result: dict[str, Any] = {
        "schema": "rp86.host-protocol.exchange/v1",
        "profile": profile.name,
        "processor": detected_processor,
        "processor_name": PROCESSOR_NAMES[detected_processor],
        "timestamp": started.isoformat(),
        "request": {
            "transport": "USB HID",
            "bytes": MESSAGE_SIZE,
            "type": request.message_type,
            "sequence": request.sequence,
            "payload": request.payload.decode("ascii"),
            "sha256": hashlib.sha256(request_record).hexdigest(),
        },
        "reply": reply_json,
        "hid": {"passed": hid_pass, "identity": hid_identity},
        "cdc_validation": {
            "role": "receive-only physical evidence",
            "passed": cdc_pass,
            "request_sequence": cdc_sequence,
            "checks_passed": list(cdc_report.passed_checks),
            "errors": list(cdc_report.errors),
            "raw_log": str(raw_path.resolve()),
            "raw_sha256": hashlib.sha256(captured).hexdigest(),
            "explanation": story,
        },
        "bus_safety": {
            "passed": "bus safety" in cdc_report.passed_checks
            and "persistent electrical state" in cdc_report.passed_checks,
            "terminal_state": "STI/HLT active; AD high-Z between cycles"
            if "persistent electrical state" in cdc_report.passed_checks
            else "unproven",
        },
        "errors": transport_errors,
        "passed": overall_pass,
    }
    json_path = output_dir / f"{profile.filename_prefix}_{timestamp}.json"
    result["result_json"] = str(json_path.resolve())
    json_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    return result, PASS_EXIT if overall_pass else VALIDATION_EXIT


def print_human_result(result: dict[str, Any]) -> None:
    request = result["request"]
    reply = result["reply"]
    processor_name = result.get("processor_name", "NEC V30")
    print(f"\n[PHYSICAL {processor_name} EXCHANGE]")
    print(f"OpenAI Codex > {request['payload']}  (HID, {request['bytes']} bytes)")
    if reply is None:
        print(f"{processor_name:<13}> <no valid HID reply>")
    else:
        print(
            f"{processor_name:<13}> {reply['payload']}  "
            f"(HID, {reply['bytes']} bytes)"
        )

    print("\n[CDC LOG EXPLANATION]")
    for index, sentence in enumerate(result["cdc_validation"]["explanation"], 1):
        print(f"{index}. {sentence}")

    cdc_errors = result["cdc_validation"]["errors"]
    if cdc_errors:
        print("\n[CDC VALIDATION ERRORS]")
        for error in cdc_errors:
            print(f"- {error}")

    print("\n[ARTIFACTS]")
    print(f"Raw CDC evidence = {result['cdc_validation']['raw_log']}")
    print(f"Codex JSON result = {result['result_json']}")
    for error in result["errors"]:
        print(f"ERROR            = {error}")
    print(f"HOST PROTOCOL RESULT = {'PASS' if result['passed'] else 'FAIL'}")
