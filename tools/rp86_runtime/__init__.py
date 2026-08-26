"""Reusable Host-side API for the RP86 physical-processor runtime."""

from .core import (
    HeartbeatStats,
    heartbeat_payload,
    hid_output_report,
    normalize_hid_input,
    simulate_v30,
    validate_device_reply,
    validate_live_reply,
    validate_reply,
)
from .device import DeviceClient
from .protocol import Message

__all__ = [
    "DeviceClient",
    "HeartbeatStats",
    "Message",
    "heartbeat_payload",
    "hid_output_report",
    "normalize_hid_input",
    "simulate_v30",
    "validate_device_reply",
    "validate_live_reply",
    "validate_reply",
]
