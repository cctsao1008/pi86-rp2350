"""Execute the real FreeRTOS #75 xQueueReceive caller path to the delayed-list boundary.

This is not a queue or scheduler simulation.  The fixture loads the production
PMAX-A P86W image, writes the minimum processor-visible queue/TCB/list pre-state,
enters the compiler-produced `prvConsumerTask` call sequence immediately before
its `xQueueReceive()` call, and single-steps the real linked Open Watcom code
until `prvAddCurrentTaskToDelayedList()` is entered.

The captured boundary state is compared with the accepted direct-call contract
from the #86/#93 fixture: DS=SS=DGROUP and caller stack words
(return IP, portMAX_DELAY, pdTRUE).
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_fixture import (
    CURRENT_TCB_OFFSET,
    ITEM_CONTAINER,
    ITEM_NEXT,
    ITEM_OWNER,
    ITEM_PREVIOUS,
    LIST_END_OFFSET,
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


# Production addresses are resolved from OMF/WLINK.  These offsets describe only
# the processor-visible pre-state required by the compiled queue path.
CONSUMER_CALL_IP_DELTA = 0x19  # 0x104F7 - _prvConsumerTask(0x104DE)
CONSUMER_BP = 0xE000
CONSUMER_LOCAL_VALUE = CONSUMER_BP - 2
CONSUMER_SP = CONSUMER_BP - 2
QUEUE_OFFSET = 0x0800
QUEUE_WAIT_SEND_OFFSET = 0x08
QUEUE_WAIT_RECEIVE_OFFSET = 0x12
QUEUE_MESSAGES_WAITING_OFFSET = 0x1C
QUEUE_LENGTH_OFFSET = 0x1E
QUEUE_ITEM_SIZE_OFFSET = 0x20
QUEUE_RX_LOCK_OFFSET = 0x22
QUEUE_TX_LOCK_OFFSET = 0x23
TCB_EVENT_ITEM_OFFSET = TCB_STATE_ITEM_OFFSET + 10
PORT_MAX_DELAY = 0xFFFF
PD_TRUE = 1


@dataclass(frozen=True)
class QueueBoundaryResult:
    registers: RegisterState
    return_ip: int
    ticks_to_wait: int
    can_block_indefinitely: int
    ready_count: int
    suspended_count: int
    state_container: int
    event_container: int
    queue_receive_list: int
    queue_messages_waiting: int
    instructions: int
    trace: str


def _write8(machine: IA16Machine, physical: int, value: int) -> None:
    machine.write_memory(physical, bytes((value & 0xFF,)))


def _setup_tcb_and_lists(machine: IA16Machine, layout: FreeRTOS75QueueLayout) -> tuple[int, int, int]:
    scheduler = layout.scheduler
    dgroup = scheduler.dgroup_base
    current_ptr = scheduler.dgroup_offset("_pxCurrentTCB")
    ready_lists = scheduler.dgroup_offset("_pxReadyTasksLists")
    suspended = scheduler.dgroup_offset("_xSuspendedTaskList")

    _write16(machine, dgroup, current_ptr, CURRENT_TCB_OFFSET)
    _init_empty_list(machine, dgroup, suspended)
    ready, _ = _init_ready_topology(machine, dgroup, ready_lists, topology="A")

    event_item = CURRENT_TCB_OFFSET + TCB_EVENT_ITEM_OFFSET
    _init_item(
        machine,
        dgroup,
        event_item,
        next_offset=0,
        previous_offset=0,
        owner_offset=CURRENT_TCB_OFFSET,
        container_offset=0,
    )
    # FreeRTOS initializes the event-list item value to
    # configMAX_PRIORITIES - uxPriority. PMAX-A consumer priority is 3 and
    # configMAX_PRIORITIES is 4, therefore the production value is 1.
    _write16(machine, dgroup, event_item, 1)
    return ready, suspended, event_item


def _setup_empty_queue(machine: IA16Machine, layout: FreeRTOS75QueueLayout) -> tuple[int, int]:
    dgroup = layout.scheduler.dgroup_base
    queue = QUEUE_OFFSET
    machine.write_memory(dgroup + queue, b"\x00" * 0x30)

    _init_empty_list(machine, dgroup, queue + QUEUE_WAIT_SEND_OFFSET)
    receive_list = queue + QUEUE_WAIT_RECEIVE_OFFSET
    _init_empty_list(machine, dgroup, receive_list)
    _write16(machine, dgroup, queue + QUEUE_MESSAGES_WAITING_OFFSET, 0)
    _write16(machine, dgroup, queue + QUEUE_LENGTH_OFFSET, 4)
    _write16(machine, dgroup, queue + QUEUE_ITEM_SIZE_OFFSET, 2)
    _write8(machine, dgroup + queue + QUEUE_RX_LOCK_OFFSET, 0xFF)
    _write8(machine, dgroup + queue + QUEUE_TX_LOCK_OFFSET, 0xFF)

    queue_handle_offset = layout.dgroup_offset(layout.queue_handle)
    _write16(machine, dgroup, queue_handle_offset, queue)
    return queue, receive_list


def _setup_consumer_call(machine: IA16Machine, layout: FreeRTOS75QueueLayout) -> None:
    scheduler = layout.scheduler
    if scheduler.text_base & 0xF or scheduler.dgroup_base & 0xF:
        raise RuntimeError("fixture requires paragraph-aligned production TEXT and DGROUP")

    text_segment = scheduler.text_base >> 4
    dgroup_segment = scheduler.dgroup_base >> 4
    call_address = layout.consumer_task + CONSUMER_CALL_IP_DELTA
    call_ip = call_address - scheduler.text_base

    # Guard the exact compiler-produced sequence used by PMAX-A:
    #   mov ax,FFFF; push ax; lea ax,[bp-2]; push ax;
    #   push word [0278]; call xQueueReceive
    expected = bytes.fromhex("B8 FF FF 50 8D 46 FE 50 FF 36 78 02 E8")
    actual = machine.read_memory(call_address, len(expected))
    if actual != expected:
        raise RuntimeError("consumer xQueueReceive call sequence changed; re-audit fixture entry")

    _write16(machine, scheduler.dgroup_base, CONSUMER_LOCAL_VALUE, 0)
    machine.write_register("cs", text_segment)
    machine.write_register("ip", call_ip)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("bp", CONSUMER_BP)
    machine.write_register("sp", CONSUMER_SP)
    machine.write_register("si", 1)
    machine.write_register("flags", 0x0202)


def run_to_delayed_list_boundary(
    p86w: Path,
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
    *,
    max_instructions: int = 1200,
) -> QueueBoundaryResult:
    layout = resolve_queue_layout(
        link_script,
        map_path,
        main_object,
        queue_object,
        tasks_object,
    )
    machine = IA16Machine()
    machine.load(load_p86w(p86w))

    ready, suspended, event_item = _setup_tcb_and_lists(machine, layout)
    queue, receive_list = _setup_empty_queue(machine, layout)
    _setup_consumer_call(machine, layout)

    recorder = TraceRecorder(max_events=max_instructions * 3)
    machine.install_trace(recorder)
    target = layout.scheduler.address("_prvAddCurrentTaskToDelayedList")

    executed = 0
    while machine.current_linear_ip() != target and executed < max_instructions:
        machine.run(instruction_count=1)
        executed += 1

    if machine.current_linear_ip() != target:
        state = machine.registers()
        raise RuntimeError(
            f"xQueueReceive caller did not reach delayed-list boundary after {executed} instructions; "
            f"stopped at {state.cs:04X}:{state.ip:04X}"
        )

    state = machine.registers()
    stack = (state.ss << 4) + state.sp
    return_ip = _read16(machine, stack)
    ticks = _read16(machine, stack + 2)
    can_block = _read16(machine, stack + 4)
    dgroup = layout.scheduler.dgroup_base

    ready_count = _read16(machine, dgroup + ready)
    suspended_count = _read16(machine, dgroup + suspended)
    state_container = _read16(
        machine,
        dgroup + CURRENT_TCB_OFFSET + TCB_STATE_ITEM_OFFSET + ITEM_CONTAINER,
    )
    event_container = _read16(machine, dgroup + event_item + ITEM_CONTAINER)
    queue_messages = _read16(machine, dgroup + queue + QUEUE_MESSAGES_WAITING_OFFSET)

    dgroup_segment = dgroup >> 4
    if state.ds != dgroup_segment or state.ss != dgroup_segment:
        raise RuntimeError(
            f"delayed-list entry segment mismatch: DS={state.ds:04X} SS={state.ss:04X} "
            f"expected DGROUP={dgroup_segment:04X}"
        )
    if ticks != PORT_MAX_DELAY or can_block != PD_TRUE:
        raise RuntimeError(
            f"delayed-list caller stack mismatch: ticks=0x{ticks:04X} canBlock={can_block}"
        )
    if ready_count != 1:
        raise RuntimeError(
            f"ready list changed before delayed-list entry: count={ready_count}, expected 1"
        )
    if suspended_count != 0:
        raise RuntimeError(
            f"suspended list changed before delayed-list entry: count={suspended_count}, expected 0"
        )
    if state_container != ready:
        raise RuntimeError(
            f"state item container changed before delayed-list entry: 0x{state_container:04X}"
        )
    if event_container != receive_list:
        raise RuntimeError(
            f"event item was not inserted into queue receive list: 0x{event_container:04X}, "
            f"expected 0x{receive_list:04X}"
        )
    if queue_messages != 0:
        raise RuntimeError("queue is no longer empty before blocking transition")

    return QueueBoundaryResult(
        registers=state,
        return_ip=return_ip,
        ticks_to_wait=ticks,
        can_block_indefinitely=can_block,
        ready_count=ready_count,
        suspended_count=suspended_count,
        state_container=state_container,
        event_container=event_container,
        queue_receive_list=receive_list,
        queue_messages_waiting=queue_messages,
        instructions=executed,
        trace=recorder.format(),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute the real FreeRTOS #75 xQueueReceive caller to the delayed-list boundary"
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
    result = run_to_delayed_list_boundary(
        args.p86w,
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
    )
    r = result.registers
    print("FreeRTOS #75 real xQueueReceive caller boundary")
    print(f"  reached after      {result.instructions} instructions")
    print(f"  CS:IP              {r.cs:04X}:{r.ip:04X}")
    print(f"  DS/ES/SS           {r.ds:04X}/{r.es:04X}/{r.ss:04X}")
    print(f"  BP/SP              {r.bp:04X}/{r.sp:04X}")
    print(f"  AX BX CX DX        {r.ax:04X} {r.bx:04X} {r.cx:04X} {r.dx:04X}")
    print(f"  SI DI FLAGS        {r.si:04X} {r.di:04X} {r.flags & 0xFFFF:04X}")
    print(f"  caller stack       ret={result.return_ip:04X} ticks={result.ticks_to_wait:04X} canBlock={result.can_block_indefinitely}")
    print(f"  ready/suspended    {result.ready_count}/{result.suspended_count}")
    print(f"  state container    0x{result.state_container:04X}")
    print(f"  event container    0x{result.event_container:04X} (queue receive list 0x{result.queue_receive_list:04X})")
    print(f"  queue messages     {result.queue_messages_waiting}")
    print("  caller-produced delayed-list entry state: COHERENT")
    if args.trace:
        print("\n=== bounded caller trace ===")
        print(result.trace)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
