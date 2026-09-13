"""Resolve the production FreeRTOS 8086 task-context symbols for issue #96."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_layout import linked_address, object_paths, segment_bases
from .freertos75_queue_layout import FreeRTOS75QueueLayout, resolve_queue_layout
from .omf import OMFObject, contribution_bases, parse_omf
from .symbols import SymbolTable


@dataclass(frozen=True)
class FreeRTOS75PortLayout:
    queue: FreeRTOS75QueueLayout
    initialise_stack: int
    install_vectors: int
    start_first_task: int
    task_exit_error: int


def _find_object(objects: tuple[OMFObject, ...], path: Path) -> OMFObject:
    target = path.resolve()
    for obj in objects:
        if obj.path is not None and obj.path.resolve() == target:
            return obj
    raise ValueError(f"object is not part of production link: {path}")


def resolve_port_layout(
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
    port_object: Path,
) -> FreeRTOS75PortLayout:
    queue_layout = resolve_queue_layout(
        link_script,
        map_path,
        main_object,
        queue_object,
        tasks_object,
    )
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    map_symbols = SymbolTable.from_wlink_map(map_text)
    placements = segment_bases(map_text)
    objects = tuple(parse_omf(path) for path in object_paths(link_script))
    bases = contribution_bases(objects)
    port = _find_object(objects, port_object)

    initialise_stack = linked_address(port, "_pxPortInitialiseStack", bases, placements)
    expected_initialise = map_symbols.address("_pxPortInitialiseStack")
    if initialise_stack != expected_initialise:
        raise RuntimeError("port-object _TEXT contribution does not match WLINK map")

    task_exit_error = linked_address(port, "_prvTaskExitError", bases, placements)
    install_vectors = map_symbols.address("_rp86PortInstallVectors")
    start_first_task = map_symbols.address("_rp86PortStartFirstTask")

    return FreeRTOS75PortLayout(
        queue=queue_layout,
        initialise_stack=initialise_stack,
        install_vectors=install_vectors,
        start_first_task=start_first_task,
        task_exit_error=task_exit_error,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Resolve FreeRTOS #75 task-context symbols")
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    parser.add_argument("--port-object", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    layout = resolve_port_layout(
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
        args.port_object,
    )
    print("FreeRTOS #75 port-context layout")
    print(f"  pxPortInitialiseStack  0x{layout.initialise_stack:05X}")
    print(f"  rp86PortInstallVectors 0x{layout.install_vectors:05X}")
    print(f"  rp86PortStartFirstTask 0x{layout.start_first_task:05X}")
    print(f"  prvTaskExitError       0x{layout.task_exit_error:05X}")
    print(f"  prvConsumerTask        0x{layout.queue.consumer_task:05X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
