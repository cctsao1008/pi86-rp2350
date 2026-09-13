"""Inject the real RP86 FreeRTOS tick across the #75 suspect instruction window.

Each case starts from a fresh production PMAX-A image, uses the real compiled
`pxPortInitialiseStack()` and NASM first-task restore, follows the real consumer
and xQueueReceive path, then injects an 8086 real-mode tick immediately before
one selected instruction in the delayed-list insertion window.

Only architectural interrupt entry is supplied by IA16Machine.  The IVT is
installed by the production `rp86PortInstallVectors()` code and the interrupt
handler/save/restore/IRET path is the real linked NASM/Open Watcom image.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_fixture import (
    CURRENT_TCB_OFFSET,
    ITEM_CONTAINER,
    ITEM_NEXT,
    ITEM_PREVIOUS,
    LIST_END_OFFSET,
    TCB_STATE_ITEM_OFFSET,
    _read16,
    _write16,
)
from .freertos75_port_context_fixture import (
    BOOTSTRAP_SP,
    MAX_RUNTIME_INSTRUCTIONS,
    _call_initialise_stack,
    _run_until,
    _validate_fabricated_frame,
)
from .freertos75_port_layout import FreeRTOS75PortLayout, resolve_port_layout
from .freertos75_queue_fixture import _setup_empty_queue, _setup_tcb_and_lists
from .loader import load_p86w
from .machine import IA16Machine, RegisterState


TICK_VECTOR = 0x21
CALL_SENTINEL_IP = 0x3E20

# The accepted production path after uxListRemove() and through the inlined
# xSuspendedTaskList insertion.  These are instruction boundaries, not source
# approximations.
SUSPECT_BOUNDARIES = (
    0x1263B,
    0x1263E,
    0x12641,
    0x12643,
    0x12647,
    0x12649,
    0x1264D,
    0x12651,
    0x12654,
    0x12658,
    0x1265B,
    0x1265E,
    0x12662,
    0x12665,
    0x12668,
    0x1266B,
    0x1266F,
    0x12672,
    0x12675,
    0x12679,
    0x1267E,
    0x12682,
)


@dataclass(frozen=True)
class InterleaveResult:
    boundary: int
    before: RegisterState
    after_iret: RegisterState
    tick_handler: int
    interrupt_instructions: int
    suspended_count_after_completion: int
    ready_count_after_completion: int
    state_container_after_completion: int
    suspended_end_next: int
    suspended_end_previous: int


def _call_noargs(
    machine: IA16Machine,
    layout: FreeRTOS75PortLayout,
    function_address: int,
) -> None:
    scheduler = layout.queue.scheduler
    text_segment = scheduler.text_base >> 4
    dgroup_segment = scheduler.dgroup_base >> 4
    stack = scheduler.dgroup_base + BOOTSTRAP_SP
    machine.write_memory(stack, CALL_SENTINEL_IP.to_bytes(2, "little"))
    machine.write_memory((text_segment << 4) + CALL_SENTINEL_IP, b"\xF4")
    machine.write_register("cs", text_segment)
    machine.write_register("ip", function_address - scheduler.text_base)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("sp", BOOTSTRAP_SP)
    machine.write_register("flags", 0x0202)
    _run_until(machine, (text_segment << 4) + CALL_SENTINEL_IP, 300)


def _fresh_running_consumer(
    p86w: Path,
    layout: FreeRTOS75PortLayout,
) -> tuple[IA16Machine, int, int]:
    scheduler = layout.queue.scheduler
    machine = IA16Machine()
    machine.load(load_p86w(p86w))
    ready, suspended, _ = _setup_tcb_and_lists(machine, layout.queue)
    _setup_empty_queue(machine, layout.queue)

    initialised_sp = _call_initialise_stack(machine, layout)
    _validate_fabricated_frame(machine, layout, initialised_sp)
    _write16(machine, scheduler.dgroup_base, CURRENT_TCB_OFFSET, initialised_sp)

    # Install the tick/yield IVT entries with the real port code before starting
    # the real first-task restore.
    _call_noargs(machine, layout, layout.install_vectors)
    raw = machine.read_memory(TICK_VECTOR * 4, 4)
    tick_offset = int.from_bytes(raw[0:2], "little")
    tick_segment = int.from_bytes(raw[2:4], "little")
    tick_handler = (tick_segment << 4) + tick_offset
    if tick_handler == 0:
        raise RuntimeError("production port did not install the tick IVT entry")

    text_segment = scheduler.text_base >> 4
    dgroup_segment = scheduler.dgroup_base >> 4
    machine.write_register("cs", text_segment)
    machine.write_register("ip", layout.start_first_task - scheduler.text_base)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("sp", BOOTSTRAP_SP)
    machine.write_register("flags", 0x0202)
    return machine, ready, suspended


def _register_tuple(state: RegisterState) -> tuple[int, ...]:
    return (
        state.ax,
        state.bx,
        state.cx,
        state.dx,
        state.si,
        state.di,
        state.bp,
        state.sp,
        state.cs,
        state.ds,
        state.es,
        state.ss,
        state.ip,
        state.flags & 0xFFFF,
    )


def run_interleave_case(
    p86w: Path,
    layout: FreeRTOS75PortLayout,
    *,
    boundary: int,
) -> InterleaveResult:
    if boundary not in SUSPECT_BOUNDARIES:
        raise ValueError(f"unsupported delayed-list boundary: 0x{boundary:05X}")

    machine, ready, suspended = _fresh_running_consumer(p86w, layout)
    scheduler = layout.queue.scheduler
    dgroup = scheduler.dgroup_base

    # Run through first-task restore, consumer, xQueueReceive and uxListRemove to
    # the exact selected instruction boundary.
    _run_until(machine, boundary, MAX_RUNTIME_INSTRUCTIONS)
    before = machine.registers()
    if (before.flags & 0x0200) == 0:
        raise RuntimeError(f"tick boundary 0x{boundary:05X} has IF clear")

    raw = machine.read_memory(TICK_VECTOR * 4, 4)
    tick_handler = (int.from_bytes(raw[2:4], "little") << 4) + int.from_bytes(raw[0:2], "little")
    original_ip = boundary

    machine.inject_real_mode_interrupt(TICK_VECTOR)
    if machine.current_linear_ip() != tick_handler:
        raise RuntimeError("interrupt injection did not enter production tick handler")

    interrupt_instructions = _run_until(machine, original_ip, 500)
    after = machine.registers()
    if _register_tuple(after) != _register_tuple(before):
        before_text = " ".join(f"{value:04X}" for value in _register_tuple(before))
        after_text = " ".join(f"{value:04X}" for value in _register_tuple(after))
        raise RuntimeError(
            f"tick at 0x{boundary:05X} changed architectural task state\n"
            f"  before {before_text}\n"
            f"  after  {after_text}"
        )

    # Continue only until the insertion itself has committed.  This avoids
    # requiring a second runnable task while still proving that the interrupt
    # did not poison the subsequent list writes.
    steps = 0
    while _read16(machine, dgroup + suspended) != 1 and steps < 80:
        machine.run(instruction_count=1)
        steps += 1
    if _read16(machine, dgroup + suspended) != 1:
        raise RuntimeError(f"tick at 0x{boundary:05X} prevented suspended-list insertion")

    current_item = CURRENT_TCB_OFFSET + TCB_STATE_ITEM_OFFSET
    end = suspended + LIST_END_OFFSET
    ready_count = _read16(machine, dgroup + ready)
    state_container = _read16(machine, dgroup + current_item + ITEM_CONTAINER)
    end_next = _read16(machine, dgroup + end + ITEM_NEXT)
    end_previous = _read16(machine, dgroup + end + ITEM_PREVIOUS)

    if ready_count != 0:
        raise RuntimeError(f"tick at 0x{boundary:05X}: ready count is {ready_count}, expected 0")
    if state_container != suspended:
        raise RuntimeError(
            f"tick at 0x{boundary:05X}: state item container 0x{state_container:04X}, "
            f"expected 0x{suspended:04X}"
        )
    if (end_next, end_previous) != (current_item, current_item):
        raise RuntimeError(
            f"tick at 0x{boundary:05X}: suspended end links are "
            f"0x{end_next:04X}/0x{end_previous:04X}"
        )

    return InterleaveResult(
        boundary=boundary,
        before=before,
        after_iret=after,
        tick_handler=tick_handler,
        interrupt_instructions=interrupt_instructions,
        suspended_count_after_completion=1,
        ready_count_after_completion=ready_count,
        state_container_after_completion=state_container,
        suspended_end_next=end_next,
        suspended_end_previous=end_previous,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Inject real FreeRTOS ticks across the #75 delayed-list instruction window"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    parser.add_argument("--port-object", type=Path, required=True)
    parser.add_argument(
        "--boundary",
        action="append",
        type=lambda value: int(value, 0),
        help="one physical instruction address; may be repeated (default: all suspect boundaries)",
    )
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
    boundaries = tuple(args.boundary) if args.boundary else SUSPECT_BOUNDARIES
    results = tuple(
        run_interleave_case(args.p86w, layout, boundary=boundary)
        for boundary in boundaries
    )

    print("FreeRTOS #75 controlled tick-interleaving evidence")
    print(f"  production tick vector 0x{TICK_VECTOR:02X}")
    print(f"  cases                  {len(results)}")
    for result in results:
        print(
            f"  0x{result.boundary:05X}: handler=0x{result.tick_handler:05X} "
            f"ISR-insns={result.interrupt_instructions:3d} "
            f"context=PRESERVED insertion=PASS"
        )
    print("  all selected interrupt boundaries preserve task state and list insertion: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
