"""Pure view-model conversion for the RP86 Web console."""

from __future__ import annotations


def processor_view(
    *, owner_mode: str, record: object, reply: dict[str, object]
) -> dict[str, object]:
    """Expose broker-owned structured state without deriving hardware state."""
    snapshot = dict(reply.get("snapshot") or {})
    identity_policy = reply.get("processor", getattr(record, "processor"))
    return {
        "ok": True,
        "owner_mode": owner_mode,
        "device_id": getattr(record, "device_id"),
        "processor": snapshot.get("native_processor") or identity_policy,
        "identity_policy": identity_policy,
        "tcp_port": getattr(record, "tcp_port"),
        "udp_port": getattr(record, "udp_port"),
        "snapshot": snapshot,
    }
