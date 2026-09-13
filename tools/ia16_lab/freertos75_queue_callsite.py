"""Resolve the compiler-produced consumer callsite into xQueueReceive().

This is a binary-evidence helper for issue #94.  It does not decode arbitrary
8086 instructions.  Instead it uses the already validated linked addresses and
finds the near CALL whose resolved target is the production xQueueReceive()
entry, then anchors the immediately preceding portMAX_DELAY argument setup.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .freertos75_queue_layout import FreeRTOS75QueueLayout, resolve_queue_layout
from .loader import load_p86w


@dataclass(frozen=True)
class QueueReceiveCallsite:
    setup_address: int
    call_address: int
    return_address: int
    queue_receive: int


def _signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def resolve_consumer_callsite(
    image: bytes,
    *,
    load_address: int,
    consumer_address: int,
    queue_receive_address: int,
    search_size: int = 0x180,
) -> QueueReceiveCallsite:
    """Locate the real near CALL from prvConsumerTask to xQueueReceive.

    The returned setup address is anchored by the Open Watcom code shape used
    for the forever-wait workload: ``mov ax,0xffff`` followed by ``push ax``.
    The CALL target itself is resolved arithmetically from the rel16 operand,
    so a coincidental byte pattern cannot be accepted as the callsite.
    """

    start = consumer_address - load_address
    if start < 0 or start >= len(image):
        raise ValueError("consumer address is outside workload image")
    end = min(start + search_size, len(image))
    window = image[start:end]

    candidates: list[int] = []
    for index in range(0, max(0, len(window) - 2)):
        if window[index] != 0xE8:
            continue
        displacement = int.from_bytes(window[index + 1 : index + 3], "little")
        call_address = consumer_address + index
        target = call_address + 3 + _signed16(displacement)
        if target == queue_receive_address:
            candidates.append(call_address)

    if len(candidates) != 1:
        raise RuntimeError(
            f"expected exactly one consumer CALL to xQueueReceive, found {len(candidates)}"
        )

    call_address = candidates[0]
    call_index = call_address - consumer_address
    prefix_start = max(0, call_index - 24)
    prefix = window[prefix_start:call_index]
    marker = bytes.fromhex("B8 FF FF 50")  # mov ax,0xffff ; push ax
    marker_index = prefix.rfind(marker)
    if marker_index < 0:
        raise RuntimeError("portMAX_DELAY argument setup was not found before xQueueReceive CALL")

    setup_address = consumer_address + prefix_start + marker_index
    return QueueReceiveCallsite(
        setup_address=setup_address,
        call_address=call_address,
        return_address=call_address + 3,
        queue_receive=queue_receive_address,
    )


def resolve_from_artifacts(
    p86w: Path,
    link_script: Path,
    map_path: Path,
    main_object: Path,
    queue_object: Path,
    tasks_object: Path,
) -> tuple[FreeRTOS75QueueLayout, QueueReceiveCallsite]:
    layout = resolve_queue_layout(
        link_script,
        map_path,
        main_object,
        queue_object,
        tasks_object,
    )
    workload = load_p86w(p86w)
    callsite = resolve_consumer_callsite(
        workload.image,
        load_address=workload.manifest.load_address,
        consumer_address=layout.consumer_task,
        queue_receive_address=layout.queue_receive,
    )
    return layout, callsite


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Resolve the production prvConsumerTask -> xQueueReceive callsite"
    )
    parser.add_argument("p86w", type=Path)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--main-object", type=Path, required=True)
    parser.add_argument("--queue-object", type=Path, required=True)
    parser.add_argument("--tasks-object", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    layout, callsite = resolve_from_artifacts(
        args.p86w,
        args.link,
        args.map_path,
        args.main_object,
        args.queue_object,
        args.tasks_object,
    )
    print("FreeRTOS #75 production consumer callsite")
    print(f"  consumer          0x{layout.consumer_task:05X}")
    print(f"  argument setup    0x{callsite.setup_address:05X}")
    print(f"  CALL              0x{callsite.call_address:05X}")
    print(f"  return            0x{callsite.return_address:05X}")
    print(f"  xQueueReceive     0x{callsite.queue_receive:05X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
