"""Cross-process ownership of one physical RP86 device."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import tempfile
from typing import BinaryIO


def ownership_directory() -> Path:
    configured = os.environ.get("RP86_BROKER_DIR")
    if configured:
        return Path(configured).expanduser()
    return Path(tempfile.gettempdir()) / "rp86-brokers"


def _ownership_path(device_id: str) -> Path:
    safe = "".join(
        character if character.isalnum() or character in "-_" else "_"
        for character in device_id
    )
    digest = hashlib.sha256(device_id.encode("utf-8")).hexdigest()[:12]
    return ownership_directory() / f"{safe}-{digest}.owner"


class DeviceOwnershipError(RuntimeError):
    """Raised when a physical device already has an active Host owner."""


class DeviceOwnership:
    """An operating-system lock held for a device owner's lifetime."""

    def __init__(self, device_id: str) -> None:
        self.device_id = device_id
        self.path = _ownership_path(device_id)
        self._file: BinaryIO | None = None

    def acquire(self) -> None:
        if self._file is not None:
            raise DeviceOwnershipError(
                f"device {self.device_id} ownership is already held"
            )
        self.path.parent.mkdir(parents=True, exist_ok=True)
        handle = self.path.open("a+b")
        if handle.tell() == 0:
            handle.write(b"\0")
            handle.flush()
        handle.seek(0)
        try:
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            handle.close()
            raise DeviceOwnershipError(
                f"device {self.device_id} already has an active owner"
            ) from exc
        self._file = handle

    def release(self) -> None:
        handle = self._file
        if handle is None:
            return
        try:
            handle.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        finally:
            handle.close()
            self._file = None

    def __enter__(self) -> "DeviceOwnership":
        self.acquire()
        return self

    def __exit__(self, *_: object) -> None:
        self.release()
