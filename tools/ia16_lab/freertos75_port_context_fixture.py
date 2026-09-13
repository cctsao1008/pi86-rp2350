"""Execute the production FreeRTOS 8086 initial task restore for issue #96.

The fixture uses the real compiled `pxPortInitialiseStack()` to fabricate the
consumer frame, points a minimal TCB at that frame, then executes the real NASM
`rp86PortStartFirstTask()` restore/IRET path.  Execution continues through the
real consumer and xQueueReceive blocking chain to the already established
`prvAddCurrentTaskToDelayedList()` boundary.

No scheduler or context-frame algorithm is reimplemented in Python; Python only
provides processor-visible object placement and assertions around the machine
code under test.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_fixture import CURRENT_TCB_OFFSET, _write16
from .freertos75_port_layout import FreeRTOS75PortLayout, resolve_port_layout
from .freertos75_queue_fixture import (
    PORT_MAX_DELAY,
    PD_TRUE,
    _setup_empty_queue,
    _setup_tcb_and_lists,
)
from .loader import load_p86w
from .machine import IA16Machine, RegisterState
from .trace import TraceRecorder


BOOTSTRAP_SP = 0xF000
TASK_STACK_TOP = 0xD800
CALL_SENTINEL_IP = 0x3E00
MAX_SETUP_INSTRUCTIONS = 400
MAX_RUNTIME_INSTRUCTIONS = 1800


@dataclass(frozen=True)
class PortContextResult:
    initialised_sp: int
    frame_words: tuple[int, ...]
    task_entry: RegisterState
    delayed_entry: RegisterState
    delayed_return_ip: int
    delayed_ticks: int
    delayed_can_block: int
    runtime_instructions: int
    trace: str


def _read16(machine: IA16Machine, physical: int) -> int:
    return int.from_bytes(machine.read_memory(physical, 2), "little")


def _stack_word(machine: IA16Machine, ss: int, sp: int, word_index: int) -> int:
    return _read16(machine, (ss << 4) + sp + word_index * 2)


def _run_until(machine: IA16Machine, target: int, maximum: int) -> int:
    executed = 0
    while machine.current_linear_ip() != target and executed < maximum:
        machine.run(instruction_count=1)
        executed += 1
    if machine.current_linear_ip() != target:
        state = machine.registers()
        raise RuntimeError(
            f"execution did not reach 0x{target:05X} after {executed} instructions; "
            f"stopped at {state.cs:04X}:{state.ip:04X}"
        )
    return executed


def _call_initialise_stack(machine: IA16Machine, layout: FreeRTOS75PortLayout) -> int:
    scheduler = layout.queue.scheduler
    text_segment = scheduler.text_base >> 4
    dgroup_segment = scheduler.dgroup_base >> 4
    initialise_ip = layout.initialise_stack - scheduler.text_base
    consumer_ip = layout.queue.consumer_task - scheduler.text_base

    # Near __cdecl call frame: return IP, pxTopOfStack, pxCode, pvParameters.
    stack = scheduler.dgroup_base + BOOTSTRAP_SP
    machine.write_memory(stack + 0, CALL_SENTINEL_IP.to_bytes(2, "little"))
    machine.write_memory(stack + 2, TASK_STACK_TOP.to_bytes(2, "little"))
    machine.write_memory(stack + 4, consumer_ip.to_bytes(2, "little"))
    machine.write_memory(stack + 6, (0).to_bytes(2, "little"))
    machine.write_memory((text_segment << 4) + CALL_SENTINEL_IP, b"\xF4")

    machine.write_register("cs", text_segment)
    machine.write_register("ip", initialise_ip)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("sp", BOOTSTRAP_SP)
    machine.write_register("flags", 0x0202)

    _run_until(
        machine,
        (text_segment << 4) + CALL_SENTINEL_IP,
        MAX_SETUP_INSTRUCTIONS,
    )
    return machine.registers().ax


def _validate_fabricated_frame(
    machine: IA16Machine,
    layout: FreeRTOS75PortLayout,
    initialised_sp: int,
) -> tuple[int, ...]:
    scheduler = layout.queue.scheduler
    dgroup_segment = scheduler.dgroup_base >> 4
    text_segment = scheduler.text_base >> 4
    consumer_ip = layout.queue.consumer_task - scheduler.text_base
    exit_ip = layout.task_exit_error - scheduler.text_base
    base = scheduler.dgroup_base + initialised_sp
    words = tuple(_read16(machine, base + index * 2) for index in range(14))
    expected = (
        0xB0B0,  # BP
        0xD1D1,  # DI
        0x5151,  # SI
        dgroup_segment,  # DS
        dgroup_segment,  # ES
        0xDDDD,  # DX
        0xCCCC,  # CX
        0xBBBB,  # BX
        0xAAAA,  # AX
        consumer_ip,  # IRET IP
        text_segment,  # IRET CS
        0x0202,  # IRET FLAGS
        exit_ip,  # ordinary near-call return address
        0x0000,  # pvParameters
    )
    if words != expected:
        raise RuntimeError(
            "pxPortInitialiseStack produced an unexpected saved frame:\n"
            f"  got      {' '.join(f'{word:04X}' for word in words)}\n"
            f"  expected {' '.join(f'{word:04X}' for word in expected)}"
        )
    return words


def run_port_context_fixture(
    p86w: Path,
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
    port_object: Path,
) -> PortContextResult:
    layout = resolve_port_layout(
        link_script,
        map_path,
        main_object,
        queue_object,
        tasks_object,
        port_object,
    )
    scheduler = layout.queue.scheduler
    machine = IA16Machine()
    machine.load(load_p86w(p86w))

    # Scheduler-visible state needed by the consumer blocking path.
    _setup_tcb_and_lists(machine, layout.queue)
    _setup_empty_queue(machine, layout.queue)

    initialised_sp = _call_initialise_stack(machine, layout)
    frame_words = _validate_fabricated_frame(machine, layout, initialised_sp)

    # pxCurrentTCB already points at CURRENT_TCB_OFFSET from the shared setup.
    # The first word of a TCB is pxTopOfStack; use the pointer returned by the
    # real pxPortInitialiseStack implementation.
    _write16(machine, scheduler.dgroup_base, CURRENT_TCB_OFFSET, initialised_sp)

    text_segment = scheduler.text_base >> 4
    dgroup_segment = scheduler.dgroup_base >> 4
    machine.write_register("cs", text_segment)
    machine.write_register("ip", layout.start_first_task - scheduler.text_base)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("sp", BOOTSTRAP_SP)
    machine.write_register("flags", 0x0202)

    recorder = TraceRecorder(max_events=MAX_RUNTIME_INSTRUCTIONS * 3)
    machine.install_trace(recorder)

    task_entry_count = _run_until(machine, layout.queue.consumer_task, MAX_RUNTIME_INSTRUCTIONS)
    task_entry = machine.registers()
    if (task_entry.ds, task_entry.es, task_entry.ss) != (
        dgroup_segment,
        dgroup_segment,
        dgroup_segment,
    ):
        raise RuntimeError(
            "initial task restore did not establish DS=ES=SS=DGROUP: "
            f"{task_entry.ds:04X}/{task_entry.es:04X}/{task_entry.ss:04X}"
        )
    if (task_entry.flags & 0x0200) == 0:
        raise RuntimeError("initial task restore entered consumer with IF clear")
    if _stack_word(machine, task_entry.ss, task_entry.sp, 0) != frame_words[12]:
        raise RuntimeError("task-entry near return IP does not match fabricated frame")
    if _stack_word(machine, task_entry.ss, task_entry.sp, 1) != 0:
        raise RuntimeError("task-entry pvParameters does not match fabricated frame")

    target = scheduler.address("_prvAddCurrentTaskToDelayedList")
    remaining = MAX_RUNTIME_INSTRUCTIONS - task_entry_count
    runtime_count = task_entry_count + _run_until(machine, target, remaining)
    delayed = machine.registers()
    stack = (delayed.ss << 4) + delayed.sp
    delayed_return = _read16(machine, stack)
    delayed_ticks = _read16(machine, stack + 2)
    delayed_can_block = _read16(machine, stack + 4)

    if (delayed.ds, delayed.es, delayed.ss) != (
        dgroup_segment,
        dgroup_segment,
        dgroup_segment,
    ):
        raise RuntimeError("restored task did not preserve DGROUP segments to delayed-list boundary")
    if delayed_ticks != PORT_MAX_DELAY or delayed_can_block != PD_TRUE:
        raise RuntimeError(
            f"restored task produced bad delayed-list arguments: ticks={delayed_ticks:04X} "
            f"canBlock={delayed_can_block}"
        )

    return PortContextResult(
        initialised_sp=initialised_sp,
        frame_words=frame_words,
        task_entry=task_entry,
        delayed_entry=delayed,
        delayed_return_ip=delayed_return,
        delayed_ticks=delayed_ticks,
        delayed_can_block=delayed_can_block,
        runtime_instructions=runtime_count,
        trace=recorder.format(),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute real FreeRTOS initial task frame/restore through the #75 caller path"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    parser.add_argument("--port-object", type=Path, required=True)
    parser.add_argument("--trace", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = run_port_context_fixture(
        args.p86w,
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
        args.port_object,
    )
    entry = result.task_entry
    delayed = result.delayed_entry
    print("FreeRTOS #75 real initial-task restore fixture")
    print(f"  initialised SP      0x{result.initialised_sp:04X}")
    print(f"  fabricated frame    {' '.join(f'{word:04X}' for word in result.frame_words)}")
    print(f"  task entry           {entry.cs:04X}:{entry.ip:04X} SP={entry.sp:04X} FLAGS={entry.flags & 0xFFFF:04X}")
    print(f"  task DS/ES/SS        {entry.ds:04X}/{entry.es:04X}/{entry.ss:04X}")
    print(f"  delayed entry        {delayed.cs:04X}:{delayed.ip:04X} SP={delayed.sp:04X} FLAGS={delayed.flags & 0xFFFF:04X}")
    print(f"  delayed DS/ES/SS     {delayed.ds:04X}/{delayed.es:04X}/{delayed.ss:04X}")
    print(f"  delayed caller stack ret={result.delayed_return_ip:04X} ticks={result.delayed_ticks:04X} canBlock={result.delayed_can_block}")
    print(f"  runtime instructions {result.runtime_instructions}")
    print("  initial task restore -> xQueueReceive -> delayed-list entry: COHERENT")
    if args.trace:
        print("\n=== bounded port-context trace ===")
        print(result.trace)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
