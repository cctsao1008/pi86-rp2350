"""Evidence storage for one RP86 Host runtime session."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
import json
from pathlib import Path
from typing import Any


@dataclass
class SessionEvidence:
    raw_path: Path
    json_path: Path
    captured: bytearray = field(default_factory=bytearray)
    events: list[dict[str, Any]] = field(default_factory=list)

    @classmethod
    def create(
        cls, output_dir: Path, started: datetime
    ) -> "SessionEvidence":
        output_dir.mkdir(parents=True, exist_ok=True)
        timestamp = started.strftime("%Y%m%d_%H%M%S%z")
        return cls(
            output_dir / f"runtime_session_{timestamp}.log",
            output_dir / f"runtime_session_{timestamp}.json",
        )

    def capture(self, data: bytes) -> None:
        self.captured.extend(data)

    def record(self, event: dict[str, Any]) -> None:
        self.events.append(event)

    def write(self, summary: dict[str, Any]) -> None:
        document = dict(summary)
        document["events"] = self.events
        document["raw_cdc_log"] = str(self.raw_path.resolve())
        self.raw_path.write_bytes(self.captured)
        self.json_path.write_text(
            json.dumps(document, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
