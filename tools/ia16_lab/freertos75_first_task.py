"""Validate the real FreeRTOS first-task stack fabrication for issue #75.

This slice executes the production Open Watcom pxPortInitialiseStack() function
from the PMAX-A image and inspects the exact frame it produces for
prvConsumerTask(). It does not reimplement the stack builder in Python; Python
only provides the call arguments and reads back the resulting processor-visible
frame.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_queue_layout import resolve_queue_layout
from .freertos75_layout import linked_address, object_paths, segment_bases
from .loader import load_p86w
from .machine import IA16Machine
from .omf import contribution_bases, parse_omf
from .symbols import SymbolTable


TASK_STACK_TOP = 0x2100
CALLER_SP = 0xF000
RETURN_SENTINEL_IP = 0x3000
PORT_INITIAL_SW = 0x0202


@dataclass(frozen=True)
class FirstTaskLayout:
    dgroup_base: int
    text_base: int
    consumer_task: int
    initialise_stack: int
    start_first_task: int
    task_exit_error: int
    px_current_tcb: int


@dataclass(frozen=True)
class FirstTaskFrame:
    returned_sp: int
    bp: int
    di: int
    si: int
    ds: int
    es: int
    dx: int
    cx: int
    bx: int
    ax: int
    ip: int
    cs: int
    flags: int
    return_ip: int
    parameter: int


def _find_object(objects, path: Path):
    target = path.resolve()
    for obj in objects:
        if obj.path is not None and obj.path.resolve() == target:
            return obj
    raise ValueError(f"object is not part of production link: {path}")


def resolve_first_task_layout(
    link_script: Path,
    map_path: Path,
    main_object: Path,
    port_object: Path,
    portasm_object: Path,
    queue_object: Path,
    tasks_object: Path,
) -> FirstTaskLayout:
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
    portasm = _find_object(objects, portasm_object)

    initialise_stack = linked_address(port, "_pxPortInitialiseStack", bases, placements)
    if initialise_stack != map_symbols.address("_pxPortInitialiseStack"):
        raise RuntimeError("port C _TEXT contribution does not match WLINK map")

    start_first_task = linked_address(portasm, "_rp86PortStartFirstTask", bases, placements)
    if start_first_task != map_symbols.address("_rp86PortStartFirstTask"):
        raise RuntimeError("portasm _TEXT contribution does not match WLINK map")

    task_exit_error = linked_address(port, "_prvTaskExitError", bases, placements)
    px_current_tcb = queue_layout.scheduler.address("_pxCurrentTCB")

    return FirstTaskLayout(
        dgroup_base=queue_layout.scheduler.dgroup_base,
        text_base=queue_layout.scheduler.text_base,
        consumer_task=queue_layout.consumer_task,
        initialise_stack=initialise_stack,
        start_first_task=start_first_task,
        task_exit_error=task_exit_error,
        px_current_tcb=px_current_tcb,
    )


def _u16(value: int) -> bytes:
    return int(value & 0xFFFF).to_bytes(2, "little")


def _read16(machine: IA16Machine, physical: int) -> int:
    return int.from_bytes(machine.read_memory(physical, 2), "little")


def execute_stack_builder(p86w: Path, layout: FirstTaskLayout) -> FirstTaskFrame:
    machine = IA16Machine()
    machine.load(load_p86w(p86w))

    if layout.text_base & 0xF or layout.dgroup_base & 0xF:
        raise RuntimeError("fixture requires paragraph-aligned TEXT and DGROUP")

    text_segment = layout.text_base >> 4
    dgroup_segment = layout.dgroup_base >> 4
    consumer_ip = layout.consumer_task - layout.text_base

    # Open Watcom near __cdecl call frame for:
    # pxPortInitialiseStack(pxTopOfStack, pxCode, pvParameters)
    stack_physical = layout.dgroup_base + CALLER_SP
    machine.write_memory(stack_physical + 0, _u16(RETURN_SENTINEL_IP))
    machine.write_memory(stack_physical + 2, _u16(TASK_STACK_TOP))
    machine.write_memory(stack_physical + 4, _u16(consumer_ip))
    machine.write_memory(stack_physical + 6, _u16(0))
    machine.write_memory(layout.text_base + RETURN_SENTINEL_IP, b"\xF4")

    machine.write_register("cs", text_segment)
    machine.write_register("ip", layout.initialise_stack - layout.text_base)
    machine.write_register("ds", dgroup_segment)
    machine.write_register("es", dgroup_segment)
    machine.write_register("ss", dgroup_segment)
    machine.write_register("sp", CALLER_SP)

    if not machine.run_until_address(layout.text_base + RETURN_SENTINEL_IP, instruction_count=200):
        raise RuntimeError("pxPortInitialiseStack did not return to the fixture sentinel")

    returned_sp = machine.registers().ax
    base = layout.dgroup_base + returned_sp
    words = tuple(_read16(machine, base + index * 2) for index in range(14))
    frame = FirstTaskFrame(
        returned_sp=returned_sp,
        bp=words[0],
        di=words[1],
        si=words[2],
        ds=words[3],
        es=words[4],
        dx=words[5],
        cx=words[6],
        bx=words[7],
        ax=words[8],
        ip=words[9],
        cs=words[10],
        flags=words[11],
        return_ip=words[12],
        parameter=words[13],
    )

    expected_exit = layout.task_exit_error - layout.text_base
    expected = {
        "bp": 0xB0B0,
        "di": 0xD1D1,
        "si": 0x5151,
        "ds": dgroup_segment,
        "es": dgroup_segment,
        "dx": 0xDDDD,
        "cx": 0xCCCC,
        "bx": 0xBBBB,
        "ax": 0xAAAA,
        "ip": consumer_ip,
        "cs": text_segment,
        "flags": PORT_INITIAL_SW,
        "return_ip": expected_exit,
        "parameter": 0,
    }
    for name, value in expected.items():
        actual = getattr(frame, name)
        if actual != value:
            raise RuntimeError(
                f"fabricated first-task frame {name}=0x{actual:04X}, expected 0x{value:04X}"
            )

    return frame


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Execute production pxPortInitialiseStack for the FreeRTOS #75 Consumer task"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--port-object", type=Path, required=True)
    parser.add_argument("--portasm-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    layout = resolve_first_task_layout(
        args.link,
        args.map_path,
        args.main_object,
        args.port_object,
        args.portasm_object,
        args.queue_object,
        args.tasks_object,
    )
    frame = execute_stack_builder(args.p86w, layout)

    print("FreeRTOS #75 first-task stack fabrication")
    print(f"  pxPortInitialiseStack   0x{layout.initialise_stack:05X}")
    print(f"  rp86PortStartFirstTask  0x{layout.start_first_task:05X}")
    print(f"  prvConsumerTask         0x{layout.consumer_task:05X}")
    print(f"  prvTaskExitError        0x{layout.task_exit_error:05X}")
    print(f"  fabricated SP           0x{frame.returned_sp:04X}")
    print(
        f"  BP/DI/SI                {frame.bp:04X}/{frame.di:04X}/{frame.si:04X}"
    )
    print(
        f"  DS/ES                   {frame.ds:04X}/{frame.es:04X}"
    )
    print(
        f"  DX/CX/BX/AX             {frame.dx:04X}/{frame.cx:04X}/{frame.bx:04X}/{frame.ax:04X}"
    )
    print(
        f"  IRET IP/CS/FLAGS        {frame.ip:04X}/{frame.cs:04X}/{frame.flags:04X}"
    )
    print(
        f"  near-call return/param  {frame.return_ip:04X}/{frame.parameter:04X}"
    )
    print("  production first-task frame: COHERENT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
