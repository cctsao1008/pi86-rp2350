"""Resolve exact linked addresses for FreeRTOS #75 local OMF symbols.

The WLINK map omits most static FreeRTOS scheduler objects.  Open Watcom OMF
objects still carry those names in LPUBDEF records, so combine object-local
symbol offsets with link-order segment contribution bases and final WLINK
segment placement.  Public/map-visible symbols are used as independent anchors
before any local address is accepted as evidence.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re

from .omf import OMFObject, contribution_bases, parse_omf
from .symbols import SymbolTable


_FILE = re.compile(r"^file\s+'([^']+)'\s*$", re.MULTILINE | re.IGNORECASE)
_SEGMENT = re.compile(
    r"^\s*(_TEXT|_DATA|_BSS)\s+\S+\s+\S+\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4,8})\b",
    re.MULTILINE,
)

TARGETS = (
    "_prvAddCurrentTaskToDelayedList",
    "_pxReadyTasksLists",
    "_xSuspendedTaskList",
    "_pxCurrentTCB",
)


def _physical(segment: str, offset: str) -> int:
    return (int(segment, 16) << 4) + int(offset, 16)


def object_paths(link_script: Path) -> tuple[Path, ...]:
    text = link_script.read_text(encoding="utf-8", errors="replace")
    paths = tuple(Path(match.group(1)) for match in _FILE.finditer(text))
    if not paths:
        raise ValueError(f"link script contains no object files: {link_script}")
    return paths


def segment_bases(map_text: str) -> dict[str, int]:
    bases = {
        match.group(1): _physical(match.group(2), match.group(3))
        for match in _SEGMENT.finditer(map_text)
    }
    missing = {"_TEXT", "_DATA", "_BSS"} - bases.keys()
    if missing:
        raise ValueError(f"WLINK map missing segment placement: {', '.join(sorted(missing))}")
    return bases


def linked_address(
    obj: OMFObject,
    symbol_name: str,
    bases: dict[tuple[Path | None, str], int],
    placements: dict[str, int],
) -> int:
    symbol = obj.symbol(symbol_name)
    if symbol is None:
        raise ValueError(f"OMF symbol not found: {symbol_name}")
    if symbol.segment_name not in placements:
        raise ValueError(f"no final placement for segment {symbol.segment_name!r}")
    contribution = bases[(obj.path, symbol.segment_name)]
    return placements[symbol.segment_name] + contribution + symbol.offset


def _visible_bss_anchor(
    objects: tuple[OMFObject, ...],
    bases: dict[tuple[Path | None, str], int],
    placements: dict[str, int],
    symbols: SymbolTable,
) -> tuple[str, int, int] | None:
    visible = {symbol.name: symbol.address for symbol in symbols.symbols}
    for obj in objects:
        for symbol in obj.symbols:
            if symbol.segment_name != "_BSS" or symbol.name not in visible:
                continue
            predicted = linked_address(obj, symbol.name, bases, placements)
            return symbol.name, predicted, visible[symbol.name]
    return None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Resolve FreeRTOS #75 local scheduler symbols from Open Watcom OMF"
    )
    parser.add_argument("--link", type=Path, required=True, help="WLINK .lnk file")
    parser.add_argument("--map", dest="map_path", type=Path, required=True, help="WLINK map")
    parser.add_argument(
        "--tasks-object",
        type=Path,
        required=True,
        help="c5_tasks.obj from the same production link",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    map_text = args.map_path.read_text(encoding="utf-8", errors="replace")
    map_symbols = SymbolTable.from_wlink_map(map_text)
    placements = segment_bases(map_text)

    paths = object_paths(args.link)
    objects = tuple(parse_omf(path) for path in paths)
    bases = contribution_bases(objects)
    tasks_path = args.tasks_object.resolve()
    tasks = next(
        (obj for obj in objects if obj.path is not None and obj.path.resolve() == tasks_path),
        None,
    )
    if tasks is None:
        raise ValueError(f"tasks object is not part of link script: {args.tasks_object}")

    # Independent anchors prove that link-order contribution arithmetic agrees
    # with the final image before local/static addresses are reported.
    text_anchor = linked_address(tasks, "_xTaskCreate", bases, placements)
    text_expected = map_symbols.address("_xTaskCreate")
    data_anchor = linked_address(tasks, "_pxCurrentTCB", bases, placements)
    data_expected = map_symbols.address("_pxCurrentTCB")
    if text_anchor != text_expected:
        raise RuntimeError(
            f"_TEXT contribution mismatch: predicted 0x{text_anchor:05X}, map 0x{text_expected:05X}"
        )
    if data_anchor != data_expected:
        raise RuntimeError(
            f"_DATA contribution mismatch: predicted 0x{data_anchor:05X}, map 0x{data_expected:05X}"
        )

    bss_anchor = _visible_bss_anchor(objects, bases, placements, map_symbols)
    if bss_anchor is None:
        raise RuntimeError("no map-visible _BSS symbol available to validate BSS contribution layout")
    bss_name, bss_predicted, bss_expected = bss_anchor
    if bss_predicted != bss_expected:
        raise RuntimeError(
            f"_BSS contribution mismatch at {bss_name}: predicted 0x{bss_predicted:05X}, "
            f"map 0x{bss_expected:05X}"
        )

    print("FreeRTOS #75 OMF linked-layout evidence")
    print(f"LINK {args.link}")
    print(f"MAP  {args.map_path}")
    print(f"TASK {args.tasks_object}")
    print("\nValidated public anchors")
    print(f"  _TEXT _xTaskCreate  0x{text_anchor:05X}")
    print(f"  _DATA _pxCurrentTCB 0x{data_anchor:05X}")
    print(f"  _BSS  {bss_name} 0x{bss_predicted:05X}")
    print("\nResolved scheduler symbols")
    for target in TARGETS:
        symbol = tasks.symbol(target)
        if symbol is None:
            raise RuntimeError(f"required tasks.c symbol missing from OMF: {target}")
        address = linked_address(tasks, target, bases, placements)
        scope = "local" if symbol.local else "public"
        contribution = bases[(tasks.path, symbol.segment_name)]
        print(
            f"  0x{address:05X} {target} "
            f"[{scope} {symbol.segment_name}+0x{contribution + symbol.offset:X}; "
            f"object+0x{symbol.offset:X}]"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
