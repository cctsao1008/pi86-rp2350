"""Command-line probe for the RP86 IA16 binary execution laboratory."""

from __future__ import annotations

import argparse

from .loader import load_p86w
from .machine import IA16Machine


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute a small number of instructions from a production RP86 P86W"
    )
    parser.add_argument("p86w", help="path to a production .P86W artifact")
    parser.add_argument(
        "--count",
        type=int,
        default=1,
        help="maximum number of instructions to execute (default: 1)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    workload = load_p86w(args.p86w)
    machine = IA16Machine()
    machine.load(workload)

    before = machine.registers()
    machine.run(instruction_count=args.count)
    after = machine.registers()

    manifest = workload.manifest
    print("RP86 IA16 Binary Laboratory")
    print(f"Image       {workload.path}")
    print(f"Load        0x{manifest.load_address:05X}")
    print(f"Entry       {manifest.entry_segment:04X}:{manifest.entry_offset:04X}")
    print(f"Stack       {manifest.stack_segment:04X}:{manifest.stack_offset:04X}")
    print(f"Instructions {args.count}")
    print(
        "Before      "
        f"CS:IP={before.cs:04X}:{before.ip:04X} "
        f"SS:SP={before.ss:04X}:{before.sp:04X}"
    )
    print(
        "After       "
        f"CS:IP={after.cs:04X}:{after.ip:04X} "
        f"SS:SP={after.ss:04X}:{after.sp:04X} "
        f"AX={after.ax:04X} BX={after.bx:04X}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
