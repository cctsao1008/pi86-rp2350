"""Pure view-model conversion for the RP86 Web console."""

from __future__ import annotations


def processor_view(
    *, owner_mode: str, record: object, reply: dict[str, object]
) -> dict[str, object]:
    """Expose broker-owned structured state without deriving hardware state."""
    return {
        "ok": True,
        "owner_mode": owner_mode,
        "device_id": getattr(record, "device_id"),
        "processor": reply.get("processor", getattr(record, "processor")),
        "tcp_port": getattr(record, "tcp_port"),
        "udp_port": getattr(record, "udp_port"),
        "snapshot": dict(reply.get("snapshot") or {}),
    }
