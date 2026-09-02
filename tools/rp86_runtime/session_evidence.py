"""Evidence storage for one RP86 Host runtime session."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime
import json
from pathlib import Path
from typing import Any

from .runtime_state import WorkloadRuntimeState
from .workload import RESULT_FLAG_NATIVE_OUTPUT_TRUNCATED, WorkloadManifest


def regression_failure_reasons(state: WorkloadRuntimeState) -> list[str]:
    checks = (
        (state.completed, "workload did not complete"),
        (state.passed, "native PASS flag absent"),
        (state.processor_identified, "validated processor identity unavailable"),
        (state.completion_reason == 2, "completion reason is not NATIVE_HLT"),
        (state.native_output == b"RESULT: PASS", "native output is not exact RESULT: PASS"),
    )
    return [reason for accepted, reason in checks if not accepted]


@dataclass
class SessionEvidence:
    raw_path: Path
    json_path: Path
    captured: bytearray = field(default_factory=bytearray)
    events: list[dict[str, Any]] = field(default_factory=list)
    workload_results: list[dict[str, Any]] = field(default_factory=list)
    errors: list[dict[str, Any]] = field(default_factory=list)
    _image: dict[str, Any] | None = None
    _image_workload_id: int | None = None
    _last_terminal: dict[str, Any] | None = None

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

    def bind_workload(
        self, workload_id: int, source: str, manifest: WorkloadManifest
    ) -> None:
        """Only bind metadata after the complete upload was accepted."""
        self._image_workload_id = workload_id
        self._image = {
            "source": source,
            "name": source.replace("\\", "/").rsplit("/", 1)[-1],
            "metadata_source": "host_accepted_upload",
            **asdict(manifest),
        }

    def failure(self, operation: str, reason: str, **details: Any) -> None:
        self.errors.append({
            "observed_at": datetime.now().astimezone().isoformat(),
            "operation": operation, "reason": reason, **details,
        })

    def workload_snapshot(self, state: WorkloadRuntimeState) -> dict[str, Any]:
        """Lossless result bytes plus explicit provenance and outcome semantics."""
        passed = state.passed if state.structured_result and state.completed else None
        failure_reason = None
        if state.lifecycle in (6, 7):
            passed = False
            failure_reason = state.completion_reason_name
        elif state.completed and passed is False:
            failure_reason = "native PASS flag absent"
        elif state.completed and not state.structured_result:
            failure_reason = "structured result unavailable"
        return {
            "workload_id": state.workload_id,
            "image": dict(self._image) if self._image_workload_id == state.workload_id
                and self._image is not None else None,
            "lifecycle": state.lifecycle_name,
            "lifecycle_code": state.lifecycle,
            "detail": state.detail,
            "clock_mode": state.clock_name,
            "cycles": state.cycles,
            "processor_state": state.processor_state,
            "processor_flags": state.processor_flags,
            "processor_identity": {
                "processor": state.processor,
                "signature": state.processor_signature,
                "validated": state.processor_identified,
                "source": "firmware_boot_aad16" if state.processor_identified else None,
                "fresh_liveness_proof": False,
            },
            "structured": state.structured_result,
            "completion_reason": state.completion_reason_name,
            "completion_reason_code": state.completion_reason,
            "result_flags": state.result_flags,
            "native_output": {
                "hex": state.native_output.hex(),
                "byte_length": len(state.native_output),
                "text": state.native_output.decode("utf-8", errors="replace"),
                "truncated": bool(state.result_flags & RESULT_FLAG_NATIVE_OUTPUT_TRUNCATED),
            },
            "passed": passed,
            "outcome": "PASS" if passed is True else "FAIL" if passed is False
                else "UNPROVEN" if state.completed else "INCOMPLETE",
            "failure_reason": failure_reason,
        }

    def observe_workload(self, state: WorkloadRuntimeState) -> None:
        if self._image_workload_id != state.workload_id:
            self._image = None
            self._image_workload_id = None
        if state.lifecycle not in (4, 5, 6, 7):
            self._last_terminal = None
            return
        snapshot = self.workload_snapshot(state)
        if snapshot != self._last_terminal:
            self.workload_results.append({
                "observed_at": datetime.now().astimezone().isoformat(),
                **snapshot,
            })
            self._last_terminal = snapshot

    def write(self, summary: dict[str, Any]) -> None:
        document = dict(summary)
        document["events"] = self.events
        document["workload_results"] = self.workload_results
        document["errors"] = self.errors
        document["raw_cdc_log"] = str(self.raw_path.resolve())
        self.raw_path.write_bytes(self.captured)
        self.json_path.write_text(
            json.dumps(document, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
