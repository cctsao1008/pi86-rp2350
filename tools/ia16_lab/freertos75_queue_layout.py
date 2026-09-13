"""Resolve production xQueueReceive caller context for FreeRTOS issue #75.

This module extends the Step-3 OMF/WLINK evidence one level outward from
prvAddCurrentTaskToDelayedList(). It resolves the real workload-local consumer
function, queue handle storage, public xQueueReceive entry, and scheduler globals
needed to execute the caller path from the same production PMAX-A link.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_layout import (
    FreeRTOS75Layout,
    linked_address,
    object_paths,
    resolve_layout,
    segment_bases,
)
from .omf import OMFObject, contribution_bases, parse_omf
from .symbols import SymbolTable


@dataclass(frozen=True)
class FreeRTOS75QueueLayout:
    scheduler: FreeRTOS75Layout
    consumer_task: int
    queue_receive: int
    queue_handle: int
    tick_count: int
    num_overflows: int
    scheduler_suspended: int
    pended_ticks: int

    def dgroup_offset(self, address: int) -> int:
        offset = address - self.scheduler.dgroup_base
        if not 0 <= offset <= 0xFFFF:
            raise ValueError("address is outside the production DGROUP near-pointer range")
        return offset


def _find_object(objects: tuple[OMFObject, ...], path: Path) -> OMFObject:
    target = path.resolve()
    for obj in objects:
        if obj.path is not None and obj.path.resolve() == target:
            return obj
    raise ValueError(f"object is not part of production link: {path}")


def resolve_queue_layout(
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
) -> FreeRTOS75QueueLayout:
    """Resolve and independently anchor the caller-side #75 production layout."""

    scheduler = resolve_layout(link_script, map_path, tasks_object)
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    map_symbols = SymbolTable.from_wlink_map(map_text)
    placements = segment_bases(map_text)

    objects = tuple(parse_omf(path) for path in object_paths(link_script))
    bases = contribution_bases(objects)
    main = _find_object(objects, main_object)
    queue = _find_object(objects, queue_object)
    tasks = _find_object(objects, tasks_object)

    main_text_anchor = linked_address(main, "_rp86_freertos_system_main", bases, placements)
    if main_text_anchor != map_symbols.address("_rp86_freertos_system_main"):
        raise RuntimeError("main-object _TEXT contribution does not match WLINK map")

    main_bss_anchor = linked_address(main, "_gRp86Telemetry", bases, placements)
    if main_bss_anchor != map_symbols.address("_gRp86Telemetry"):
        raise RuntimeError("main-object _BSS contribution does not match WLINK map")

    queue_text_anchor = linked_address(queue, "_xQueueReceive", bases, placements)
    if queue_text_anchor != map_symbols.address("_xQueueReceive"):
        raise RuntimeError("queue-object _TEXT contribution does not match WLINK map")

    consumer_task = linked_address(main, "_prvConsumerTask", bases, placements)
    queue_handle = linked_address(main, "_xQueue", bases, placements)
    queue_receive = queue_text_anchor

    task_globals = {
        name: linked_address(tasks, name, bases, placements)
        for name in (
            "_xTickCount",
            "_xNumOfOverflows",
            "_uxSchedulerSuspended",
            "_xPendedTicks",
        )
    }

    for address in (queue_handle, *task_globals.values()):
        offset = address - scheduler.dgroup_base
        if not 0 <= offset <= 0xFFFF:
            raise RuntimeError("caller fixture state is outside DGROUP")

    return FreeRTOS75QueueLayout(
        scheduler=scheduler,
        consumer_task=consumer_task,
        queue_receive=queue_receive,
        queue_handle=queue_handle,
        tick_count=task_globals["_xTickCount"],
        num_overflows=task_globals["_xNumOfOverflows"],
        scheduler_suspended=task_globals["_uxSchedulerSuspended"],
        pended_ticks=task_globals["_xPendedTicks"],
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Resolve production FreeRTOS #75 xQueueReceive caller symbols"
    )
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    layout = resolve_queue_layout(
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
    )

    print("FreeRTOS #75 xQueueReceive caller-layout evidence")
    print(f"  DGROUP base       0x{layout.scheduler.dgroup_base:05X}")
    print(f"  consumer task     0x{layout.consumer_task:05X}")
    print(f"  xQueueReceive     0x{layout.queue_receive:05X}")
    print(
        f"  xQueue handle     0x{layout.queue_handle:05X} "
        f"[DGROUP+0x{layout.dgroup_offset(layout.queue_handle):04X}]"
    )
    print(
        f"  delayed-list fn   0x{layout.scheduler.address('_prvAddCurrentTaskToDelayedList'):05X}"
    )
    print("  scheduler globals")
    for name, address in (
        ("xTickCount", layout.tick_count),
        ("xNumOfOverflows", layout.num_overflows),
        ("uxSchedulerSuspended", layout.scheduler_suspended),
        ("xPendedTicks", layout.pended_ticks),
    ):
        print(f"    {name:<20} 0x{address:05X} [DGROUP+0x{layout.dgroup_offset(address):04X}]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
