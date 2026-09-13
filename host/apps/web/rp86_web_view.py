"""Pure presentation helpers for the RP86 Web console."""

from __future__ import annotations


def processor_view(*, owner_mode, record, reply):
    snapshot = dict(reply.get("snapshot") or {})
    processor = snapshot.get("processor")
    processor_alive = bool(processor)
    return {
        "ok": bool(reply.get("ok")),
        "owner_mode": owner_mode,
        "device_id": getattr(record, "device_id", None),
        "processor": processor,
        "processor_alive": processor_alive,
        "snapshot": snapshot,
        "error": reply.get("error"),
    }
