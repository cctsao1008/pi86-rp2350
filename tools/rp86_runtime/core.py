"""RP86 records, reply validation, and heartbeat statistics."""

from .common import *

def heartbeat_payload(sequence: int, nonce: int | None = None) -> bytes:
    """Build the seven native V30 mailbox words for one fresh liveness proof."""
    if nonce is None:
        nonce = secrets.randbits(64)
    return struct.pack("<2sIQ", b"HB", sequence & 0xFFFFFFFF, nonce)


@dataclass
class HeartbeatStats:
    completed: int = 0
    lost: int = 0
    last_ms: float = 0.0
    minimum_ms: float = float("inf")
    maximum_ms: float = 0.0
    total_ms: float = 0.0

    def accept(self, latency_ms: float) -> None:
        self.completed += 1
        self.last_ms = latency_ms
        self.minimum_ms = min(self.minimum_ms, latency_ms)
        self.maximum_ms = max(self.maximum_ms, latency_ms)
        self.total_ms += latency_ms

    @property
    def average_ms(self) -> float:
        return self.total_ms / self.completed if self.completed else 0.0


def simulate_v30(record: bytes) -> bytes:
    request = Message.decode(record)
    if request.message_type != TYPE_HELLO or request.payload != CANONICAL_GREETING:
        raise ValueError("simulated V30 rejected the greeting")
    return Message(TYPE_TEXT, request.sequence, CANONICAL_REPLY).encode()


def hid_output_report(record: bytes) -> bytes:
    """Add HIDAPI's required zero report-ID byte to one ABI record."""
    if len(record) != MESSAGE_SIZE:
        raise ValueError(f"record must be exactly {MESSAGE_SIZE} bytes")
    return b"\0" + record


def normalize_hid_input(report: bytes) -> bytes:
    """Accept HIDAPI's platform representation, never a partial record."""
    if len(report) == MESSAGE_SIZE + 1 and report[0] == 0:
        report = report[1:]
    if len(report) != MESSAGE_SIZE:
        raise ValueError(
            f"HID reply must contain exactly {MESSAGE_SIZE} ABI bytes; got {len(report)}"
        )
    return report


def validate_reply(
    record: bytes,
    sequence: int,
    expected_type: int = TYPE_TEXT,
    expected_payload: bytes = CANONICAL_REPLY,
) -> Message:
    reply = Message.decode(normalize_hid_input(record))
    if reply.message_type != expected_type:
        raise ValueError(f"unexpected V30 reply type: {reply.message_type}")
    if reply.sequence != sequence:
        raise ValueError(f"V30 reply sequence mismatch: {reply.sequence} != {sequence}")
    if reply.payload != expected_payload:
        raise ValueError(f"unexpected V30 reply payload: {reply.payload!r}")
    return reply


def validate_live_reply(
    record: bytes,
    request: Message,
    expected_processor: str | None = None,
) -> Message:
    expected_type = TYPE_RESULT if request.message_type == TYPE_COMMAND else TYPE_HEARTBEAT
    expected_payload = COMMAND_REPLY if request.message_type == TYPE_COMMAND else HEARTBEAT_REPLY
    reply = Message.decode(normalize_hid_input(record))
    if reply.message_type != expected_type:
        raise ValueError(f"unexpected native reply type: {reply.message_type}")
    if reply.sequence != request.sequence:
        raise ValueError(
            f"native reply sequence mismatch: {reply.sequence} != {request.sequence}"
        )
    if reply.status != STATUS_OK:
        raise ValueError(f"V30 live reply status is not OK: {reply.status}")
    witness = NativeServiceWitness.decode(reply.payload)
    if witness.service_type != request.message_type:
        raise ValueError(
            "native witness service type mismatch: "
            f"{witness.service_type} != {request.message_type}"
        )
    from .calculator import is_calculator_payload

    calculator_reply = (
        request.message_type == TYPE_COMMAND and
        is_calculator_payload(request.payload) and
        witness.text.startswith(b"CALC ")
    )
    if witness.text != expected_payload and not calculator_reply:
        raise ValueError(f"unexpected native reply text: {witness.text!r}")
    if expected_processor is not None and witness.processor != expected_processor:
        raise ValueError(
            "native processor identity mismatch: "
            f"AAD16={witness.processor or 'unknown'} host={expected_processor}"
        )
    return reply


def validate_device_reply(
    record: bytes,
    request: Message,
    expected_processor: str | None = None,
) -> Message:
    """Validate either the deployed heartbeat ABI or the workload ABI."""
    if request.message_type in (TYPE_COMMAND, TYPE_HEARTBEAT):
        return validate_live_reply(record, request, expected_processor)

    if request.message_type == TYPE_FILESYSTEM_REQUEST:
        reply = Message.decode(normalize_hid_input(record))
        if reply.message_type != TYPE_FILESYSTEM_RESULT:
            raise ValueError(
                f"unexpected filesystem reply type: {reply.message_type}"
            )
        if reply.sequence != request.sequence:
            raise ValueError(
                f"filesystem reply sequence mismatch: {reply.sequence} != "
                f"{request.sequence}"
            )
        return reply

    if request.message_type == TYPE_MEMORY_REQUEST:
        reply = Message.decode(normalize_hid_input(record))
        if reply.message_type != TYPE_MEMORY_RESULT:
            raise ValueError(f"unexpected memory reply type: {reply.message_type}")
        if reply.sequence != request.sequence:
            raise ValueError(
                f"memory reply sequence mismatch: {reply.sequence} != "
                f"{request.sequence}"
            )
        return reply

    if request.message_type not in (
        TYPE_WORKLOAD_BEGIN,
        TYPE_WORKLOAD_DATA,
        TYPE_WORKLOAD_COMMIT,
        TYPE_WORKLOAD_CONTROL,
    ):
        raise ValueError(f"unsupported request type: {request.message_type}")
    reply = Message.decode(normalize_hid_input(record))
    allowed_types = (TYPE_WORKLOAD_RESULT, TYPE_WORKLOAD_STATUS)
    if reply.message_type not in allowed_types:
        raise ValueError(f"unexpected workload reply type: {reply.message_type}")
    if reply.sequence != request.sequence:
        raise ValueError(
            f"workload reply sequence mismatch: {reply.sequence} != {request.sequence}"
        )
    if reply.status != STATUS_OK:
        raise ValueError(f"workload request status is not OK: {reply.status}")
    return reply
