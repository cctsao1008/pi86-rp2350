"""Execute the real linked FreeRTOS #75 indefinite-block path from a minimal state.

This fixture is deliberately not a FreeRTOS simulator.  It loads the production
PMAX-A P86W image, resolves the real linked kernel addresses from OMF/WLINK
artifacts, writes only the processor-visible list/TCB pre-state needed by
prvAddCurrentTaskToDelayedList(), and executes that real Open Watcom machine
code under the IA16 lab.

Two states use the same tasks.c machine code:

A: the current task is the sole member of its ready list.
B: one same-priority peer remains after the current task is removed.

The comparison asks whether that ready-list topology changes the instruction
path that reaches the inlined listINSERT_END() for xSuspendedTaskList.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_layout import FreeRTOS75Layout, resolve_layout
from .loader import load_p86w
from .machine import IA16Machine
from .trace import InstructionTrace, TraceRecorder


LIST_SIZE = 10
LIST_END_OFFSET = 4
LIST_ITEM_SIZE = 10
TCB_STATE_ITEM_OFFSET = 2
ITEM_NEXT = 2
ITEM_PREVIOUS = 4
ITEM_OWNER = 6
ITEM_CONTAINER = 8

CURRENT_TCB_OFFSET = 0x1000
PEER_TCB_OFFSET = 0x1100
STACK_OFFSET = 0xF000
RETURN_IP = 0x3000
PORT_MAX_DELAY = 0xFFFF
PD_TRUE = 1


@dataclass(frozen=True)
class FixtureResult:
    topology: str
    ready_count: int
    suspended_count: int
    item_container: int
    suspended_end_next: int
    suspended_end_previous: int
    post_remove_path: tuple[int, ...]
    trace: str


def _u16(value: int) -> bytes:
    return int(value & 0xFFFF).to_bytes(2, "little")


def _read16(machine: IA16Machine, physical: int) -> int:
    return int.from_bytes(machine.read_memory(physical, 2), "little")


def _write16(machine: IA16Machine, dgroup_base: int, offset: int, value: int) -> None:
    machine.write_memory(dgroup_base + offset, _u16(value))


def _init_empty_list(machine: IA16Machine, dgroup_base: int, list_offset: int) -> None:
    end = list_offset + LIST_END_OFFSET
    _write16(machine, dgroup_base, list_offset + 0, 0)
    _write16(machine, dgroup_base, list_offset + 2, end)
    _write16(machine, dgroup_base, end + 0, 0xFFFF)
    _write16(machine, dgroup_base, end + 2, end)
    _write16(machine, dgroup_base, end + 4, end)


def _init_item(
    machine: IA16Machine,
    dgroup_base: int,
    item_offset: int,
    *,
    next_offset: int,
    previous_offset: int,
    owner_offset: int,
    container_offset: int,
) -> None:
    _write16(machine, dgroup_base, item_offset + 0, 0)
    _write16(machine, dgroup_base, item_offset + ITEM_NEXT, next_offset)
    _write16(machine, dgroup_base, item_offset + ITEM_PREVIOUS, previous_offset)
    _write16(machine, dgroup_base, item_offset + ITEM_OWNER, owner_offset)
    _write16(machine, dgroup_base, item_offset + ITEM_CONTAINER, container_offset)


def _init_ready_topology(
    machine: IA16Machine,
    dgroup_base: int,
    ready_lists_offset: int,
    *,
    topology: str,
) -> tuple[int, int]:
    if topology == "A":
        priority = 3
        count = 1
    elif topology == "B":
        priority = 2
        count = 2
    else:
        raise ValueError("topology must be A or B")

    ready = ready_lists_offset + priority * LIST_SIZE
    end = ready + LIST_END_OFFSET
    current_item = CURRENT_TCB_OFFSET + TCB_STATE_ITEM_OFFSET
    peer_item = PEER_TCB_OFFSET + TCB_STATE_ITEM_OFFSET

    _init_empty_list(machine, dgroup_base, ready)
    _write16(machine, dgroup_base, ready, count)

    if topology == "A":
        _write16(machine, dgroup_base, end + ITEM_NEXT, current_item)
        _write16(machine, dgroup_base, end + ITEM_PREVIOUS, current_item)
        _init_item(
            machine,
            dgroup_base,
            current_item,
            next_offset=end,
            previous_offset=end,
            owner_offset=CURRENT_TCB_OFFSET,
            container_offset=ready,
        )
    else:
        _write16(machine, dgroup_base, end + ITEM_NEXT, current_item)
        _write16(machine, dgroup_base, end + ITEM_PREVIOUS, peer_item)
        _init_item(
            machine,
            dgroup_base,
            current_item,
            next_offset=peer_item,
            previous_offset=end,
            owner_offset=CURRENT_TCB_OFFSET,
            container_offset=ready,
        )
        _init_item(
            machine,
            dgroup_base,
            peer_item,
            next_offset=end,
            previous_offset=current_item,
            owner_offset=PEER_TCB_OFFSET,
            container_offset=ready,
        )

    return ready, count


def _setup_call(machine: IA16Machine, layout: FreeRTOS75Layout) -> None:
    if layout.text_base & 0xF or layout.dgroup_base & 0xF:
        raise RuntimeError("fixture requires paragraph-aligned production TEXT and DGROUP")

    text_segment = layout.text_base >> 4
    dgroup_segment = layout.dgroup_base >> 4
    entry_ip = layout.address("_prvAddCurrentTaskToDelayedList") - layout.text_base
    if not 0 <= entry_ip <= 0xFFFF:
        raise RuntimeError("resolved function is outside the near code segment")

    expected_prefix = bytes.fromhex("56 57 55 89 E5 83 EC 04")
    actual_prefix = machine.read_memory(layout.address("_prvAddCurrentTaskToDelayedList"), len(expected_prefix))
    if actual_prefix != expected_prefix:
        raise RuntimeError(
            "production function entry changed; re-audit calling convention before running fixture"
        )

    # Open Watcom small-model cdecl evidence from the production disassembly:
    # after pushing SI/DI/BP, [BP+8] is xTicksToWait and [BP+0A] is
    # xCanBlockIndefinitely.  Therefore the caller stack at entry is:
    # return IP, arg1, arg2.
    stack_physical = layout.dgroup_base + STACK_OFFSET
    machine.write_memory(stack_physical + 0, _u16(RETURN_IP))
    machine.write_memory(stack_physical + 2, _u16(PORT_MAX_DELAY))
    machine.write_memory(stack_physical + 4, _u16(PD_TRUE))

    # Return into one byte beyond the production image and stop cleanly on HLT.
    machine.write_memory((text_segment << 4) + RETURN_IP, b"\xF4")

    machine.write_register("cs", text_segment)
    machine.write_register("ip", entry_ip)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("sp", STACK_OFFSET)


def run_fixture(
    p86w: Path,
    link_script: Path,
    map_path: Path,
    tasks_object: Path,
    *,
    topology: str,
) -> FixtureResult:
    layout = resolve_layout(link_script, map_path, tasks_object)
    machine = IA16Machine()
    machine.load(load_p86w(p86w))

    current_ptr = layout.dgroup_offset("_pxCurrentTCB")
    ready_lists = layout.dgroup_offset("_pxReadyTasksLists")
    suspended = layout.dgroup_offset("_xSuspendedTaskList")
    suspended_end = suspended + LIST_END_OFFSET
    current_item = CURRENT_TCB_OFFSET + TCB_STATE_ITEM_OFFSET

    _write16(machine, layout.dgroup_base, current_ptr, CURRENT_TCB_OFFSET)
    _init_empty_list(machine, layout.dgroup_base, suspended)
    ready, initial_ready_count = _init_ready_topology(
        machine,
        layout.dgroup_base,
        ready_lists,
        topology=topology,
    )
    _setup_call(machine, layout)

    recorder = TraceRecorder(max_events=1200)
    machine.install_trace(recorder)
    machine.run(instruction_count=160)

    ready_count = _read16(machine, layout.dgroup_base + ready)
    suspended_count = _read16(machine, layout.dgroup_base + suspended)
    item_container = _read16(
        machine,
        layout.dgroup_base + current_item + ITEM_CONTAINER,
    )
    end_next = _read16(machine, layout.dgroup_base + suspended_end + ITEM_NEXT)
    end_previous = _read16(machine, layout.dgroup_base + suspended_end + ITEM_PREVIOUS)

    expected_ready = initial_ready_count - 1
    if ready_count != expected_ready:
        raise RuntimeError(
            f"topology {topology}: ready count {ready_count}, expected {expected_ready}"
        )
    if suspended_count != 1:
        raise RuntimeError(f"topology {topology}: suspended count is {suspended_count}, expected 1")
    if item_container != suspended:
        raise RuntimeError(
            f"topology {topology}: state item container 0x{item_container:04X}, "
            f"expected suspended list 0x{suspended:04X}"
        )
    if (end_next, end_previous) != (current_item, current_item):
        raise RuntimeError(
            f"topology {topology}: suspended end links are "
            f"0x{end_next:04X}/0x{end_previous:04X}, expected current item 0x{current_item:04X}"
        )

    # The source-level if around uxListRemove has an empty body in this build
    # because configUSE_PORT_OPTIMISED_TASK_SELECTION == 0.  Compare the actual
    # production instruction stream after the call returns and through the
    # indefinite list insertion.  The ready-list count is deliberately excluded
    # from this assertion; only executed instruction addresses are compared.
    path = tuple(
        event.address
        for event in recorder.events
        if isinstance(event, InstructionTrace) and 0x1263B <= event.address <= 0x12682
    )
    if not path:
        raise RuntimeError("fixture did not execute the expected indefinite-block instruction window")

    return FixtureResult(
        topology=topology,
        ready_count=ready_count,
        suspended_count=suspended_count,
        item_container=item_container,
        suspended_end_next=end_next,
        suspended_end_previous=end_previous,
        post_remove_path=path,
        trace=recorder.format(),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute real FreeRTOS #75 delayed-list code from minimal A/B list states"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    parser.add_argument("--trace", action="store_true", help="print bounded machine traces")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    results = tuple(
        run_fixture(
            args.p86w,
            args.link,
            args.map_path,
            args.tasks_object,
            topology=topology,
        )
        for topology in ("A", "B")
    )

    print("FreeRTOS #75 real-machine-code fixture")
    for result in results:
        print(
            f"  {result.topology}: ready={result.ready_count} suspended={result.suspended_count} "
            f"container=0x{result.item_container:04X} path-insns={len(result.post_remove_path)}"
        )
    if results[0].post_remove_path != results[1].post_remove_path:
        raise RuntimeError("A/B post-uxListRemove instruction paths differ")
    print("  A/B post-uxListRemove instruction path: IDENTICAL")
    print("  Both states complete the real xSuspendedTaskList insertion: PASS")

    if args.trace:
        for result in results:
            print(f"\n=== topology {result.topology} trace ===")
            print(result.trace)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
