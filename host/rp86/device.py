"""Stable client API shared by RP86 Host tools and scripts."""

from __future__ import annotations

import secrets
from typing import Any

from .broker import BrokerClient, BrokerRecord, discover_brokers, select_broker


class DeviceClient:
    """Connect to the single-owner RP86 broker for one physical device.

    This API deliberately speaks to the local broker instead of opening CDC or
    HID itself.  Any number of Host tools may therefore share one processor
    without fighting over the USB interfaces.
    """

    def __init__(self, record: BrokerRecord, client_id: str | None = None) -> None:
        self.record = record
        self.client_id = client_id or f"rp86-{secrets.token_hex(4)}"
        self._client = BrokerClient(record, self.client_id)

    @classmethod
    def discover(
        cls, device_id: str | None = None, client_id: str | None = None
    ) -> "DeviceClient":
        record = select_broker(discover_brokers(), device_id)
        if record is None:
            raise RuntimeError("no active RP86 Host broker was found")
        return cls(record, client_id)

    @property
    def device_id(self) -> str:
        return self.record.device_id

    @property
    def processor(self) -> str:
        return self.record.processor

    def info(self) -> dict[str, Any]:
        return self._require_ok(self._client.hello())

    def exchange(self, record: bytes, timeout: float = 5.0) -> bytes:
        request_id = secrets.token_hex(8)
        response = self._require_ok(
            self._client.exchange(record, request_id, timeout)
        )
        encoded = response.get("reply_hex")
        if not isinstance(encoded, str):
            raise RuntimeError("broker exchange did not return a record")
        result = bytes.fromhex(encoded)
        if len(result) != 64:
            raise RuntimeError("broker returned an invalid RP86 record length")
        return result

    def control(self, command: str, timeout: float = 5.0) -> dict[str, Any]:
        if command not in ("status", "bootloader", "reboot"):
            raise ValueError(f"unsupported RP86 control command: {command}")
        return self._require_ok(
            self._client.control(command, secrets.token_hex(8), timeout)
        )

    @staticmethod
    def _require_ok(response: dict[str, Any]) -> dict[str, Any]:
        if not response.get("ok"):
            raise RuntimeError(str(response.get("error") or "RP86 broker request failed"))
        return response
