"""Sequence-bound request/reply exchange over the RP86 HID transport."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import time
from typing import Any

from .core import validate_device_reply
from .protocol import MESSAGE_SIZE, Message
from .transport import hid_output_report


RequestExchange = Callable[
    [Message], tuple[Message | None, float, str | None]
]


@dataclass(frozen=True)
class ExchangeResult:
    reply: Message | None
    latency_ms: float
    error: str | None


def exchange_hid_request(
    hid_device: Any,
    request: Message,
    *,
    timeout: float,
    expected_processor: str | None,
    service_transport: Callable[[], str | None],
) -> ExchangeResult:
    """Send one request and ignore records belonging to earlier requests."""
    began = time.monotonic()
    stale_reply_error: str | None = None
    try:
        while bytes(hid_device.read(MESSAGE_SIZE + 1)):
            pass
        written = hid_device.write(hid_output_report(request.encode()))
        if written != MESSAGE_SIZE + 1:
            return ExchangeResult(
                None, 0.0, f"short HID write: {written}/{MESSAGE_SIZE + 1} bytes"
            )
        deadline = began + timeout
        while time.monotonic() <= deadline:
            transport_error = service_transport()
            if transport_error is not None:
                return ExchangeResult(
                    None, (time.monotonic() - began) * 1000.0, transport_error
                )
            candidate = bytes(hid_device.read(MESSAGE_SIZE + 1))
            if candidate:
                try:
                    reply = validate_device_reply(
                        candidate, request, expected_processor
                    )
                except ValueError as exc:
                    stale_reply_error = str(exc)
                    continue
                latency_ms = (time.monotonic() - began) * 1000.0
                drain_deadline = time.monotonic() + 0.05
                while time.monotonic() < drain_deadline:
                    if service_transport() is not None:
                        break
                    time.sleep(0.001)
                return ExchangeResult(reply, latency_ms, None)
            time.sleep(0.001)
    except OSError as exc:
        return ExchangeResult(
            None,
            (time.monotonic() - began) * 1000.0,
            f"USB HID disconnected: {exc}",
        )
    error = "device reply timeout"
    if stale_reply_error is not None:
        error += f" after stale HID reply: {stale_reply_error}"
    return ExchangeResult(None, (time.monotonic() - began) * 1000.0, error)
