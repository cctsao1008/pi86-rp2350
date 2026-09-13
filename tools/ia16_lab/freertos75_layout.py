"""Resolve exact linked addresses for FreeRTOS #75 local OMF symbols.

The WLINK map omits most static FreeRTOS scheduler objects. Open Watcom OMF
objects still carry those names in LPUBDEF records, so combine object-local
symbol offsets with link-order segment contribution bases and final WLINK
segment placement. Public/map-visible symbols are used as independent anchors
before any local address is accepted as evidence.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re

from .omf import OMFObject, contribution_bases, parse_omf
from .symbols import SymbolTable


_FILE = re.compile(r"^file\s+'([^']+)'\s*$", re.MULTILINE | re.IGNORECASE)
_SEGMENT = re.compile(
    r"^\s*(_TEXT|_DATA|_BSS)\s+\S+\s+\S+\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4,8})\b",
    re.MULTILINE,
)
_DGROUP = re.compile(
    r"^DGROUP\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4,8})\b",
    re.MULTILINE,
)

TARGETS = (
    "_prvAddCurrentTaskToDelayedList",
    "_pxReadyTasksLists",
    "_xSuspendedTaskList",
    "_pxCurrentTCB",
)


@dataclass(frozen=True)
class FreeRTOS75Layout:
    text_base: int
    dgroup_base: int
    addresses: dict[str, int]
    bss_anchor_name: str
    bss_anchor_address: int

    def address(self, name: str) -> int:
        try:
            return self.addresses[name]
        except KeyError as exc:
            raise ValueError(f"resolved FreeRTOS #75 symbol not found: {name}") from exc

    def dgroup_offset(self, name: str) -> int:
        offset = self.address(name) - self.dgroup_base
        if not 0 <= offset <= 0xFFFF:
            raise ValueError(f"{name} is outside the resolved DGROUP near-pointer range")
        return offset


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


def dgroup_base(map_text: str) -> int:
    match = _DGROUP.search(map_text)
    if match is None:
        raise ValueError("WLINK map contains no DGROUP placement")
    return _physical(match.group(1), match.group(2))


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


def resolve_layout(link_script: Path, map_path: Path, tasks_object: Path) -> FreeRTOS75Layout:
    """Resolve and validate the linked scheduler layout used by one production image."""
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    map_symbols = SymbolTable.from_wlink_map(map_text)
    placements = segment_bases(map_text)

    paths = object_paths(link_script)
    objects = tuple(parse_omf(path) for path in paths)
    bases = contribution_bases(objects)
    tasks_path = tasks_object.resolve()
    tasks = next(
        (obj for obj in objects if obj.path is not None and obj.path.resolve() == tasks_path),
        None,
    )
    if tasks is None:
        raise ValueError(f"tasks object is not part of link script: {tasks_object}")

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

    addresses: dict[str, int] = {}
    for target in TARGETS:
        if tasks.symbol(target) is None:
            raise RuntimeError(f"required tasks.c symbol missing from OMF: {target}")
        addresses[target] = linked_address(tasks, target, bases, placements)

    layout = FreeRTOS75Layout(
        text_base=placements["_TEXT"],
        dgroup_base=dgroup_base(map_text),
        addresses=addresses,
        bss_anchor_name=bss_name,
        bss_anchor_address=bss_predicted,
    )

    # The C16 kernel uses near data pointers. Prove all scheduler data needed by
    # the fixture is representable from the production DGROUP base.
    for name in ("_pxReadyTasksLists", "_xSuspendedTaskList", "_pxCurrentTCB"):
        layout.dgroup_offset(name)
    return layout


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
    layout = resolve_layout(args.link, args.map_path, args.tasks_object)

    print("FreeRTOS #75 OMF linked-layout evidence")
    print(f"LINK {args.link}")
    print(f"MAP  {args.map_path}")
    print(f"TASK {args.tasks_object}")
    print("\nValidated public anchors")
    print(f"  _TEXT _xTaskCreate  {SymbolTable.from_wlink_map(args.map_path.read_text()).address('_xTaskCreate'):#07x}")
    print(f"  _DATA _pxCurrentTCB {layout.address('_pxCurrentTCB'):#07x}")
    print(f"  _BSS  {layout.bss_anchor_name} {layout.bss_anchor_address:#07x}")
    print(f"  DGROUP base          {layout.dgroup_base:#07x}")
    print("\nResolved scheduler symbols")
    for target in TARGETS:
        address = layout.address(target)
        if target == "_prvAddCurrentTaskToDelayedList":
            detail = f"_TEXT+0x{address - layout.text_base:X}"
        else:
            detail = f"DGROUP+0x{address - layout.dgroup_base:X}"
        print(f"  0x{address:05X} {target} [{detail}]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
