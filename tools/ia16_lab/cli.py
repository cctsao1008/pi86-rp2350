"""Command-line probe for the RP86 IA16 binary execution laboratory."""

from __future__ import annotations

import argparse
from pathlib import Path

from .loader import load_p86w
from .machine import IA16Machine
from .symbols import SymbolTable
from .trace import TraceRecorder, WatchRange


def _int(value: str) -> int:
    return int(value, 0)


def _watch(value: str, symbols: SymbolTable | None) -> WatchRange:
    if symbols is not None:
        try:
            start, end = symbols.span(value)
            return WatchRange(start, end, value)
        except ValueError:
            pass
    if "-" in value:
        first, last = value.split("-", 1)
        start = _int(first)
        end_inclusive = _int(last)
        return WatchRange(start, end_inclusive + 1, value)
    start = _int(value)
    return WatchRange(start, start + 1, value)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute a bounded instruction window from a production RP86 P86W"
    )
    parser.add_argument("p86w", help="path to a production .P86W artifact")
    parser.add_argument(
        "--count",
        type=int,
        default=1,
        help="maximum number of instructions to execute (default: 1)",
    )
    parser.add_argument("--trace", action="store_true", help="record instruction and memory events")
    parser.add_argument("--map", type=Path, help="optional Open Watcom/WLINK map for symbol names")
    parser.add_argument(
        "--watch",
        action="append",
        default=[],
        metavar="ADDR[-END]|SYMBOL",
        help="restrict trace to an address/range or map symbol; repeat as needed",
    )
    parser.add_argument(
        "--trace-limit",
        type=int,
        default=1000,
        help="maximum retained trace events (default: 1000)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    workload = load_p86w(args.p86w)
    machine = IA16Machine()
    machine.load(workload)

    symbols = None
    if args.map is not None:
        symbols = SymbolTable.from_wlink_map(args.map.read_text(encoding="utf-8", errors="replace"))

    recorder = None
    if args.trace or args.watch or symbols is not None:
        watches = tuple(_watch(value, symbols) for value in args.watch)
        recorder = TraceRecorder(symbols=symbols, watches=watches, max_events=args.trace_limit)
        machine.install_trace(recorder)

    before = machine.registers()
    machine.run(instruction_count=args.count)
    after = machine.registers()

    manifest = workload.manifest
    print("RP86 IA16 Binary Laboratory")
    print(f"Image        {workload.path}")
    print(f"Load         0x{manifest.load_address:05X}")
    print(f"Entry        {manifest.entry_segment:04X}:{manifest.entry_offset:04X}")
    print(f"Stack        {manifest.stack_segment:04X}:{manifest.stack_offset:04X}")
    print(f"Instructions {args.count}")
    print(
        "Before       "
        f"CS:IP={before.cs:04X}:{before.ip:04X} "
        f"SS:SP={before.ss:04X}:{before.sp:04X}"
    )
    print(
        "After        "
        f"CS:IP={after.cs:04X}:{after.ip:04X} "
        f"SS:SP={after.ss:04X}:{after.sp:04X} "
        f"AX={after.ax:04X} BX={after.bx:04X}"
    )
    if recorder is not None:
        print("\nTrace")
        print(recorder.format() or "(no matching events)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
