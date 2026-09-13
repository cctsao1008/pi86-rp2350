"""Inspect the real compiled FreeRTOS #75 artifact before building a fixture.

This module deliberately does not model FreeRTOS.  It extracts the linker-map
and startup-execution evidence needed to construct the Step-3 fixture from the
same production .P86W image that runs on the physical processor.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from .loader import load_p86w
from .machine import IA16Machine
from .symbols import Symbol, SymbolTable
from .trace import TraceRecorder


FREERTOS75_SYMBOL_FRAGMENTS = (
    "prvAddCurrentTaskToDelayedList",
    "uxListRemove",
    "xSuspendedTaskList",
    "pxCurrentTCB",
    "pxReadyTasksLists",
)


def matching_symbols(
    symbols: SymbolTable,
    fragments: tuple[str, ...] = FREERTOS75_SYMBOL_FRAGMENTS,
) -> tuple[Symbol, ...]:
    """Return map symbols relevant to the #75 list transition."""
    lowered = tuple(fragment.lower() for fragment in fragments)
    return tuple(
        symbol
        for symbol in symbols.symbols
        if any(fragment in symbol.name.lower() for fragment in lowered)
    )


def matching_map_lines(
    text: str,
    fragments: tuple[str, ...] = FREERTOS75_SYMBOL_FRAGMENTS,
) -> tuple[str, ...]:
    """Return raw WLINK lines too, including forms the generic parser may skip."""
    lowered = tuple(fragment.lower() for fragment in fragments)
    return tuple(
        line
        for line in text.splitlines()
        if any(fragment in line.lower() for fragment in lowered)
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Collect map/startup evidence for the FreeRTOS #75 IA16 vertical slice"
    )
    parser.add_argument("p86w", type=Path, help="production FreeRTOS .P86W")
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument(
        "--count",
        type=int,
        default=64,
        help="bounded startup instruction count to trace (default: 64)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    map_text = args.map_path.read_text(encoding="utf-8", errors="replace")
    symbols = SymbolTable.from_wlink_map(map_text)

    print("FreeRTOS #75 IA16 evidence probe")
    print(f"P86W {args.p86w}")
    print(f"MAP  {args.map_path}")
    print("\nRelevant parsed symbols")
    found = matching_symbols(symbols)
    if found:
        for symbol in found:
            print(f"  0x{symbol.address:05X} {symbol.name}")
    else:
        print("  (none parsed)")

    print("\nRelevant raw map lines")
    lines = matching_map_lines(map_text)
    if lines:
        for line in lines:
            print(f"  {line}")
    else:
        print("  (none)")

    workload = load_p86w(args.p86w)
    machine = IA16Machine()
    machine.load(workload)
    recorder = TraceRecorder(symbols=symbols, max_events=max(args.count * 2, 64))
    machine.install_trace(recorder)

    print(f"\nBounded startup trace ({args.count} instructions maximum)")
    try:
        machine.run(instruction_count=args.count)
    except Exception as exc:  # evidence probe: preserve the first model boundary
        print(f"STOP {type(exc).__name__}: {exc}")
    print(recorder.format() or "(no trace events)")

    # The global objects are required for the Step-3 fixture.  A static helper
    # function may or may not be emitted by WLINK under its source-level name,
    # so absence of prvAddCurrentTaskToDelayedList is evidence, not a failure.
    required = ("xSuspendedTaskList", "pxCurrentTCB", "pxReadyTasksLists")
    names = "\n".join((*[symbol.name for symbol in symbols.symbols], *lines)).lower()
    missing = tuple(name for name in required if name.lower() not in names)
    if missing:
        print("\nMissing required fixture symbols: " + ", ".join(missing))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
