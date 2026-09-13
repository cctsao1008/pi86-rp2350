"""Stateful Host client for RP2350 workload lifecycle requests."""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass, field

from .protocol import Message
from .request_channel import RequestExchange
from .runtime_state import RequestSequence, WorkloadRuntimeState


@dataclass(frozen=True)
class WorkloadTransactionResult:
    success: bool
    count: int
    failed_index: int | None = None
    error: str | None = None
    latencies_ms: tuple[float, ...] = field(default_factory=tuple)


class WorkloadClient:
    """Apply workload records while keeping sequence and state coherent."""

    def __init__(
        self,
        exchange: RequestExchange,
        sequence: RequestSequence,
        state: WorkloadRuntimeState,
        on_state_change: Callable[[], None],
        on_activity: Callable[[], None],
    ) -> None:
        self._exchange = exchange
        self._sequence = sequence
        self.state = state
        self._on_state_change = on_state_change
        self._on_activity = on_activity

    def transact(
        self, records: Sequence[Message]
    ) -> WorkloadTransactionResult:
        latencies: list[float] = []
        for index, request in enumerate(records, 1):
            reply, latency_ms, error = self._exchange(request)
            if reply is None:
                return WorkloadTransactionResult(
                    False,
                    len(records),
                    failed_index=index,
                    error=error or "workload exchange failed",
                    latencies_ms=tuple(latencies),
                )
            self._sequence.advance_after(request.sequence)
            latencies.append(latency_ms)
            try:
                self.state.update_from_payload(reply.payload)
            except ValueError as exc:
                return WorkloadTransactionResult(
                    False,
                    len(records),
                    failed_index=index,
                    error=f"invalid status payload: {exc}",
                    latencies_ms=tuple(latencies),
                )
            self._on_state_change()
        self._on_activity()
        return WorkloadTransactionResult(
            True, len(records), latencies_ms=tuple(latencies)
        )
