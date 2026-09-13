"""Machine-level trace collection for the IA16 binary laboratory."""

from __future__ import annotations

from dataclasses import dataclass

from .machine import RegisterState
from .symbols import SymbolTable


@dataclass(frozen=True)
class WatchRange:
    start: int
    end: int
    label: str | None = None

    def __post_init__(self) -> None:
        if self.start < 0 or self.end <= self.start:
            raise ValueError("watch range must satisfy 0 <= start < end")

    def overlaps(self, address: int, size: int = 1) -> bool:
        return address < self.end and address + max(size, 1) > self.start


@dataclass(frozen=True)
class InstructionTrace:
    address: int
    size: int
    registers: RegisterState
    symbol: str | None


@dataclass(frozen=True)
class MemoryTrace:
    kind: str
    address: int
    size: int
    value: int
    registers: RegisterState
    segment_candidates: tuple[str, ...]
    symbol: str | None


class TraceRecorder:
    """Bounded instruction/memory recorder with optional address filtering."""

    def __init__(
        self,
        *,
        symbols: SymbolTable | None = None,
        watches: tuple[WatchRange, ...] = (),
        max_events: int = 1000,
    ) -> None:
        if max_events <= 0:
            raise ValueError("max_events must be positive")
        self.symbols = symbols
        self.watches = watches
        self.max_events = max_events
        self.events: list[InstructionTrace | MemoryTrace] = []
        self.dropped = 0

    def interested(self, address: int, size: int = 1) -> bool:
        return not self.watches or any(watch.overlaps(address, size) for watch in self.watches)

    def instruction(self, address: int, size: int, registers: RegisterState) -> None:
        if not self.interested(address, size):
            return
        self._append(
            InstructionTrace(
                address=address,
                size=size,
                registers=registers,
                symbol=self.symbols.resolve(address) if self.symbols else None,
            )
        )

    def memory(
        self,
        kind: str,
        address: int,
        size: int,
        value: int,
        registers: RegisterState,
    ) -> None:
        if not self.interested(address, size):
            return
        self._append(
            MemoryTrace(
                kind=kind,
                address=address,
                size=size,
                value=value,
                registers=registers,
                segment_candidates=_segment_candidates(address, registers),
                symbol=self.symbols.resolve(address) if self.symbols else None,
            )
        )

    def _append(self, event: InstructionTrace | MemoryTrace) -> None:
        if len(self.events) >= self.max_events:
            self.dropped += 1
            return
        self.events.append(event)

    def format(self) -> str:
        lines: list[str] = []
        for event in self.events:
            if isinstance(event, InstructionTrace):
                state = event.registers
                symbol = f"  {event.symbol}" if event.symbol else ""
                lines.append(
                    f"EXEC   {state.cs:04X}:{state.ip:04X} "
                    f"phys=0x{event.address:05X} size={event.size}{symbol}"
                )
                lines.append(
                    f"       AX={state.ax:04X} BX={state.bx:04X} CX={state.cx:04X} "
                    f"DX={state.dx:04X} DS={state.ds:04X} ES={state.es:04X} "
                    f"SS:SP={state.ss:04X}:{state.sp:04X}"
                )
            else:
                segments = ",".join(event.segment_candidates) or "segment=?"
                symbol = f"  {event.symbol}" if event.symbol else ""
                lines.append(
                    f"{event.kind:<6} 0x{event.address:05X} size={event.size} "
                    f"value=0x{event.value:0{max(2, event.size * 2)}X} "
                    f"[{segments}]{symbol}"
                )
        if self.dropped:
            lines.append(f"... {self.dropped} trace events dropped by max_events")
        return "\n".join(lines)


def _segment_candidates(address: int, state: RegisterState) -> tuple[str, ...]:
    """Return segment:offset candidates for one physical address.

    Unicorn memory hooks expose the resolved physical address, not the decoded
    effective-address segment.  Candidate segments preserve useful evidence
    without pretending that the exact addressing mode has been decoded.
    """
    candidates: list[str] = []
    for name, segment in (("DS", state.ds), ("ES", state.es), ("SS", state.ss), ("CS", state.cs)):
        offset = address - (segment << 4)
        if 0 <= offset <= 0xFFFF:
            candidates.append(f"{name}:{offset:04X}")
    return tuple(candidates)
