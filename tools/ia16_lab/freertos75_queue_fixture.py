"""Execute the real FreeRTOS #75 xQueueReceive caller path to the delayed-list boundary.

This is not a queue or scheduler simulator. The fixture loads the production
PMAX-A P86W image, writes the minimum processor-visible queue/TCB/list state,
starts at the compiler-produced call sequence inside prvConsumerTask(), and
runs the actual xQueueReceive -> vTaskPlaceOnEventList ->
prvAddCurrentTaskToDelayedList machine-code chain.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_fixture import (
    CURRENT_TCB_OFFSET,
    ITEM_CONTAINER,
    TCB_STATE_ITEM_OFFSET,
    _init_empty_list,
    _init_item,
    _init_ready_topology,
    _read16,
    _write16,
)
from .freertos75_queue_layout import FreeRTOS75QueueLayout, resolve_queue_layout
from .loader import load_p86w
from .machine import IA16Machine, RegisterState
from .trace import TraceRecorder


QUEUE_OFFSET = 0x0800
QUEUE_BUFFER_OFFSET = 0x0900
QUEUE_WAIT_SEND_OFFSET = 0x08
QUEUE_WAIT_RECEIVE_OFFSET = 0x12
QUEUE_MESSAGES_WAITING_OFFSET = 0x1C
QUEUE_LENGTH_OFFSET = 0x1E
QUEUE_ITEM_SIZE_OFFSET = 0x20
QUEUE_RX_LOCK_OFFSET = 0x22
QUEUE_TX_LOCK_OFFSET = 0x23
TCB_EVENT_ITEM_OFFSET = 0x0C
CONSUMER_BP = 0xE100
PORT_MAX_DELAY = 0xFFFF
PD_TRUE = 1
FLAGS_IF = 0x0200


@dataclass(frozen=True)
class QueueBoundaryResult:
    call_site: int
    entry: RegisterState
    ticks_to_wait: int
    can_block_indefinitely: int
    ready_count: int
    receive_event_count: int
    suspended_count: int
    event_item_container: int
    scheduler_suspended: int
    rx_lock: int
    tx_lock: int
    trace: str


def _u16(value: int) -> bytes:
    return int(value & 0xFFFF).to_bytes(2, "little")


def _read8(machine: IA16Machine, physical: int) -> int:
    return machine.read_memory(physical, 1)[0]


def _init_queue(machine: IA16Machine, base: int) -> None:
    machine.write_memory(base + QUEUE_OFFSET, b"\x00" * 0x30)
    _write16(machine, base, QUEUE_OFFSET + 0x00, QUEUE_BUFFER_OFFSET)
    _write16(machine, base, QUEUE_OFFSET + 0x02, QUEUE_BUFFER_OFFSET)
    _write16(machine, base, QUEUE_OFFSET + 0x04, QUEUE_BUFFER_OFFSET + 8)
    _write16(machine, base, QUEUE_OFFSET + 0x06, QUEUE_BUFFER_OFFSET + 6)
    _init_empty_list(machine, base, QUEUE_OFFSET + QUEUE_WAIT_SEND_OFFSET)
    _init_empty_list(machine, base, QUEUE_OFFSET + QUEUE_WAIT_RECEIVE_OFFSET)
    _write16(machine, base, QUEUE_OFFSET + QUEUE_MESSAGES_WAITING_OFFSET, 0)
    _write16(machine, base, QUEUE_OFFSET + QUEUE_LENGTH_OFFSET, 4)
    _write16(machine, base, QUEUE_OFFSET + QUEUE_ITEM_SIZE_OFFSET, 2)
    machine.write_memory(base + QUEUE_OFFSET + QUEUE_RX_LOCK_OFFSET, b"\xff\xff")


def _init_event_item(machine: IA16Machine, base: int) -> None:
    item = CURRENT_TCB_OFFSET + TCB_EVENT_ITEM_OFFSET
    _init_item(
        machine,
        base,
        item,
        next_offset=0,
        previous_offset=0,
        owner_offset=CURRENT_TCB_OFFSET,
        container_offset=0,
    )
    # configMAX_PRIORITIES(4) - PMAX-A consumer priority(3) = 1.
    _write16(machine, base, item, 1)


def _find_consumer_call_site(machine: IA16Machine, layout: FreeRTOS75QueueLayout) -> int:
    queue_handle = layout.dgroup_offset(layout.queue_handle)
    prefix = bytes.fromhex("B8 FF FF 50 8D 46 FE 50 FF 36") + _u16(queue_handle)
    body = machine.read_memory(layout.consumer_task, 0x80)
    index = body.find(prefix)
    if index < 0:
        raise RuntimeError("compiler-produced xQueueReceive caller sequence was not found")

    call_offset = index + len(prefix)
    if body[call_offset] != 0xE8:
        raise RuntimeError("consumer sequence no longer uses the audited near call")
    relative = int.from_bytes(body[call_offset + 1 : call_offset + 3], "little", signed=True)
    next_instruction = layout.consumer_task + call_offset + 3
    if next_instruction + relative != layout.queue_receive:
        raise RuntimeError("consumer call sequence does not target resolved xQueueReceive")
    return layout.consumer_task + index


def _prepare_machine(machine: IA16Machine, layout: FreeRTOS75QueueLayout) -> tuple[int, int]:
    base = layout.scheduler.dgroup_base
    if layout.scheduler.text_base & 0xF or base & 0xF:
        raise RuntimeError("fixture requires paragraph-aligned production TEXT and DGROUP")

    current_ptr = layout.scheduler.dgroup_offset("_pxCurrentTCB")
    ready_lists = layout.scheduler.dgroup_offset("_pxReadyTasksLists")
    suspended = layout.scheduler.dgroup_offset("_xSuspendedTaskList")

    _write16(machine, base, current_ptr, CURRENT_TCB_OFFSET)
    _write16(machine, base, layout.dgroup_offset(layout.queue_handle), QUEUE_OFFSET)
    _write16(machine, base, layout.dgroup_offset(layout.tick_count), 0)
    _write16(machine, base, layout.dgroup_offset(layout.num_overflows), 0)
    _write16(machine, base, layout.dgroup_offset(layout.scheduler_suspended), 0)
    _write16(machine, base, layout.dgroup_offset(layout.pended_ticks), 0)

    _init_empty_list(machine, base, suspended)
    ready, _ = _init_ready_topology(machine, base, ready_lists, topology="A")
    _init_event_item(machine, base)
    _init_queue(machine, base)

    call_site = _find_consumer_call_site(machine, layout)
    machine.write_memory(base + CONSUMER_BP - 2, _u16(0))
    machine.write_register("cs", layout.scheduler.text_base >> 4)
    machine.write_register("ip", call_site - layout.scheduler.text_base)
    machine.write_register("ds", base >> 4)
    machine.write_register("es", base >> 4)
    machine.write_register("ss", base >> 4)
    machine.write_register("bp", CONSUMER_BP)
    machine.write_register("sp", CONSUMER_BP - 2)
    machine.write_register("si", 1)
    machine.write_register("flags", 0x0202)
    return call_site, ready


def run_queue_boundary_fixture(
    p86w: Path,
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
) -> QueueBoundaryResult:
    layout = resolve_queue_layout(link_script, map_path, main_object, queue_object, tasks_object)
    machine = IA16Machine()
    machine.load(load_p86w(p86w))
    call_site, ready = _prepare_machine(machine, layout)

    recorder = TraceRecorder(max_events=1800)
    machine.install_trace(recorder)
    delayed = layout.scheduler.address("_prvAddCurrentTaskToDelayedList")
    if not machine.run_until_address(delayed, instruction_count=500):
        raise RuntimeError("real xQueueReceive caller path did not reach delayed-list entry")

    state = machine.registers()
    base = layout.scheduler.dgroup_base
    stack = base + state.sp
    ticks = _read16(machine, stack + 2)
    can_block = _read16(machine, stack + 4)
    receive_list = QUEUE_OFFSET + QUEUE_WAIT_RECEIVE_OFFSET
    event_item = CURRENT_TCB_OFFSET + TCB_EVENT_ITEM_OFFSET
    suspended = layout.scheduler.dgroup_offset("_xSuspendedTaskList")

    ready_count = _read16(machine, base + ready)
    receive_count = _read16(machine, base + receive_list)
    suspended_count = _read16(machine, base + suspended)
    event_container = _read16(machine, base + event_item + ITEM_CONTAINER)
    scheduler_suspended = _read16(machine, layout.scheduler_suspended)
    rx_lock = _read8(machine, base + QUEUE_OFFSET + QUEUE_RX_LOCK_OFFSET)
    tx_lock = _read8(machine, base + QUEUE_OFFSET + QUEUE_TX_LOCK_OFFSET)

    dgroup_segment = base >> 4
    if (state.ds, state.es, state.ss) != (dgroup_segment, dgroup_segment, dgroup_segment):
        raise RuntimeError("caller reached delayed-list entry with incoherent segment state")
    if ticks != PORT_MAX_DELAY or can_block != PD_TRUE:
        raise RuntimeError(
            f"delayed-list arguments are 0x{ticks:04X}/0x{can_block:04X}, expected FFFF/0001"
        )
    if (state.flags & FLAGS_IF) == 0:
        raise RuntimeError("caller reached delayed-list entry with IF unexpectedly clear")
    if ready_count != 1:
        raise RuntimeError(f"ready-list count changed before delayed-list entry: {ready_count}")
    if receive_count != 1 or event_container != receive_list:
        raise RuntimeError("vTaskPlaceOnEventList did not create the expected queue event-list state")
    if suspended_count != 0:
        raise RuntimeError("suspended list changed before delayed-list function executed")
    if scheduler_suspended != 1:
        raise RuntimeError(f"scheduler suspended depth is {scheduler_suspended}, expected 1")
    if (rx_lock, tx_lock) != (0, 0):
        raise RuntimeError(f"queue lock state is {rx_lock}/{tx_lock}, expected 0/0")

    # Use the actual caller return word rather than a hard-coded linked address.
    # This executes the delayed-list function exactly once and stops before the
    # caller's stack cleanup, proving the compiler-produced entry state completes
    # the same indefinite-block transition as the direct #86/#93 fixture.
    return_ip = _read16(machine, stack)
    return_address = layout.scheduler.text_base + return_ip
    if not machine.run_until_address(return_address, instruction_count=180):
        raise RuntimeError("delayed-list function did not return to vTaskPlaceOnEventList")

    if _read16(machine, base + ready) != 0:
        raise RuntimeError("real caller state did not remove current task from ready list")
    if _read16(machine, base + suspended) != 1:
        raise RuntimeError("real caller state did not insert current task into suspended list")
    state_item = CURRENT_TCB_OFFSET + TCB_STATE_ITEM_OFFSET
    if _read16(machine, base + state_item + ITEM_CONTAINER) != suspended:
        raise RuntimeError("state-list item container does not point at xSuspendedTaskList")

    return QueueBoundaryResult(
        call_site=call_site,
        entry=state,
        ticks_to_wait=ticks,
        can_block_indefinitely=can_block,
        ready_count=ready_count,
        receive_event_count=receive_count,
        suspended_count=suspended_count,
        event_item_container=event_container,
        scheduler_suspended=scheduler_suspended,
        rx_lock=rx_lock,
        tx_lock=tx_lock,
        trace=recorder.format(),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute the production xQueueReceive caller context for FreeRTOS #75"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    parser.add_argument("--trace", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = run_queue_boundary_fixture(
        args.p86w,
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
    )
    state = result.entry
    print("FreeRTOS #75 real xQueueReceive caller fixture")
    print(f"  consumer call site      0x{result.call_site:05X}")
    print(f"  delayed-list entry      {state.cs:04X}:{state.ip:04X}")
    print(f"  DS/ES/SS                {state.ds:04X}/{state.es:04X}/{state.ss:04X}")
    print(f"  BP/SP                   {state.bp:04X}/{state.sp:04X}")
    print(f"  FLAGS                   0x{state.flags & 0xFFFF:04X}")
    print(f"  delayed args            0x{result.ticks_to_wait:04X}/0x{result.can_block_indefinitely:04X}")
    print(f"  ready/event/suspended   {result.ready_count}/{result.receive_event_count}/{result.suspended_count}")
    print(f"  scheduler suspended     {result.scheduler_suspended}")
    print(f"  queue locks             {result.rx_lock}/{result.tx_lock}")
    print("  caller-produced delayed-list insertion: PASS")
    if args.trace:
        print("\n=== bounded caller trace ===")
        print(result.trace)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
