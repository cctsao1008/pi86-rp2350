"""Execute the real RP86 FreeRTOS first-task context restore for issue #75.

The fixture uses the production PMAX-A P86W and linked Open Watcom/NASM code.
It first executes the real pxPortInitialiseStack() implementation to fabricate a
consumer task frame, then points pxCurrentTCB at that frame and executes the real
_rp86PortStartFirstTask path through its IRET.  RP86 trace-port OUT instructions
are observed but do not model any device or timing behavior.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_layout import linked_address, object_paths, segment_bases
from .freertos75_queue_layout import FreeRTOS75QueueLayout, resolve_queue_layout
from .loader import load_p86w
from .machine import IA16Machine, RegisterState
from .omf import OMFObject, contribution_bases, parse_omf
from .symbols import SymbolTable


TCB_OFFSET = 0x1000
TASK_STACK_TOP = 0xD000
CALL_STACK = 0xE000
RETURN_IP = 0xF000
PORT_INITIAL_SW = 0x0202


@dataclass(frozen=True)
class FreeRTOS75TaskContextLayout:
    queue: FreeRTOS75QueueLayout
    initialise_stack: int
    start_first_task: int
    task_exit_error: int


@dataclass(frozen=True)
class FirstTaskContextResult:
    frame_sp: int
    frame_words: tuple[int, ...]
    restored: RegisterState
    task_return_ip: int
    task_parameter: int
    io_writes: tuple[tuple[int, int, int], ...]


def _find_object(objects: tuple[OMFObject, ...], path: Path) -> OMFObject:
    target = path.resolve()
    for obj in objects:
        if obj.path is not None and obj.path.resolve() == target:
            return obj
    raise ValueError(f"object is not part of production link: {path}")


def _read16(machine: IA16Machine, physical: int) -> int:
    return int.from_bytes(machine.read_memory(physical, 2), "little")


def _write16(machine: IA16Machine, physical: int, value: int) -> None:
    machine.write_memory(physical, int(value & 0xFFFF).to_bytes(2, "little"))


def resolve_task_context_layout(
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
    port_object: Path,
) -> FreeRTOS75TaskContextLayout:
    queue = resolve_queue_layout(
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
    if initialise_stack != map_symbols.address("_pxPortInitialiseStack"):
        raise RuntimeError("port-object _TEXT contribution does not match WLINK map")

    task_exit_error = linked_address(port, "_prvTaskExitError", bases, placements)
    start_first_task = map_symbols.address("_rp86PortStartFirstTask")

    return FreeRTOS75TaskContextLayout(
        queue=queue,
        initialise_stack=initialise_stack,
        start_first_task=start_first_task,
        task_exit_error=task_exit_error,
    )


def _execute_stack_initialiser(
    machine: IA16Machine,
    layout: FreeRTOS75TaskContextLayout,
) -> tuple[int, tuple[int, ...]]:
    base = layout.queue.scheduler.dgroup_base
    text = layout.queue.scheduler.text_base
    code_segment = text >> 4
    data_segment = base >> 4
    consumer_ip = layout.queue.consumer_task - text

    # Open Watcom near __cdecl entry stack: return IP, arg1, arg2, arg3.
    stack = base + CALL_STACK
    _write16(machine, stack + 0, RETURN_IP)
    _write16(machine, stack + 2, TASK_STACK_TOP)
    _write16(machine, stack + 4, consumer_ip)
    _write16(machine, stack + 6, 0)
    machine.write_memory(text + RETURN_IP, b"\xF4")

    machine.write_register("cs", code_segment)
    machine.write_register("ip", layout.initialise_stack - text)
    machine.write_register("ds", data_segment)
    machine.write_register("es", data_segment)
    machine.write_register("ss", data_segment)
    machine.write_register("sp", CALL_STACK)

    if not machine.run_until_address(text + RETURN_IP, instruction_count=180):
        raise RuntimeError("pxPortInitialiseStack did not return to fixture boundary")

    frame_sp = machine.registers().ax
    expected_sp = TASK_STACK_TOP - 26
    if frame_sp != expected_sp:
        raise RuntimeError(
            f"pxPortInitialiseStack returned 0x{frame_sp:04X}, expected 0x{expected_sp:04X}"
        )

    frame = tuple(_read16(machine, base + frame_sp + offset) for offset in range(0, 28, 2))
    expected = (
        0xB0B0,
        0xD1D1,
        0x5151,
        data_segment,
        data_segment,
        0xDDDD,
        0xCCCC,
        0xBBBB,
        0xAAAA,
        consumer_ip,
        code_segment,
        PORT_INITIAL_SW,
        layout.task_exit_error - text,
        0x0000,
    )
    if frame != expected:
        raise RuntimeError(
            "production pxPortInitialiseStack frame differs from audited 8086 restore contract"
        )
    return frame_sp, frame


def run_first_task_context_fixture(
    p86w: Path,
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
    port_object: Path,
) -> FirstTaskContextResult:
    layout = resolve_task_context_layout(
        link_script,
        map_path,
        main_object,
        queue_object,
        tasks_object,
        port_object,
    )
    machine = IA16Machine()
    machine.load(load_p86w(p86w))

    frame_sp, frame = _execute_stack_initialiser(machine, layout)
    base = layout.queue.scheduler.dgroup_base
    text = layout.queue.scheduler.text_base
    data_segment = base >> 4
    code_segment = text >> 4

    px_current = layout.queue.scheduler.dgroup_offset("_pxCurrentTCB")
    _write16(machine, base + px_current, TCB_OFFSET)
    _write16(machine, base + TCB_OFFSET, frame_sp)

    writes: list[tuple[int, int, int]] = []
    machine.install_io_handlers(
        on_out=lambda port, size, value: writes.append((port, size, value))
    )

    # Give the port a valid temporary execution stack. The production routine
    # replaces SS:SP with the selected task frame before restore.
    machine.write_register("cs", code_segment)
    machine.write_register("ip", layout.start_first_task - text)
    machine.write_register("ds", 0x0000)
    machine.write_register("es", 0x0000)
    machine.write_register("ss", data_segment)
    machine.write_register("sp", CALL_STACK)
    machine.write_register("flags", 0x0202)

    if not machine.run_until_address(layout.queue.consumer_task, instruction_count=240):
        raise RuntimeError("_rp86PortStartFirstTask did not IRET into prvConsumerTask")

    state = machine.registers()
    expected_registers = (
        state.ax,
        state.bx,
        state.cx,
        state.dx,
        state.si,
        state.di,
        state.bp,
    )
    if expected_registers != (0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD, 0x5151, 0xD1D1, 0xB0B0):
        raise RuntimeError(f"first-task restore register mismatch: {expected_registers!r}")
    if (state.cs, state.ip) != (code_segment, layout.queue.consumer_task - text):
        raise RuntimeError("first-task restore did not land at prvConsumerTask")
    if (state.ds, state.es, state.ss) != (data_segment, data_segment, data_segment):
        raise RuntimeError("first-task restore did not preserve DS=ES=SS=DGROUP")
    if (state.flags & 0xFFFF) != PORT_INITIAL_SW:
        raise RuntimeError(f"first-task FLAGS are 0x{state.flags & 0xFFFF:04X}, expected 0202")

    # After nine POPs + IRET, SP must point at the ordinary near-call return IP;
    # pvParameters follows at SP+2.
    task_return = _read16(machine, base + state.sp)
    task_parameter = _read16(machine, base + state.sp + 2)
    expected_return = layout.task_exit_error - text
    if task_return != expected_return or task_parameter != 0:
        raise RuntimeError(
            f"task near-call frame is return=0x{task_return:04X} param=0x{task_parameter:04X}, "
            f"expected 0x{expected_return:04X}/0000"
        )

    return FirstTaskContextResult(
        frame_sp=frame_sp,
        frame_words=frame,
        restored=state,
        task_return_ip=task_return,
        task_parameter=task_parameter,
        io_writes=tuple(writes),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute the production RP86 FreeRTOS first-task restore for issue #75"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    parser.add_argument("--port-object", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = run_first_task_context_fixture(
        args.p86w,
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
        args.port_object,
    )
    state = result.restored
    print("FreeRTOS #75 first-task context evidence")
    print(f"  fabricated frame SP   0x{result.frame_sp:04X}")
    print("  frame words           " + " ".join(f"{word:04X}" for word in result.frame_words))
    print(f"  restored CS:IP        {state.cs:04X}:{state.ip:04X}")
    print(f"  restored DS/ES/SS     {state.ds:04X}/{state.es:04X}/{state.ss:04X}")
    print(f"  restored SP/BP        {state.sp:04X}/{state.bp:04X}")
    print(f"  restored FLAGS        0x{state.flags & 0xFFFF:04X}")
    print(f"  task return/parameter 0x{result.task_return_ip:04X}/0x{result.task_parameter:04X}")
    print(f"  observed OUT writes   {len(result.io_writes)}")
    print("  production first-task restore contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
