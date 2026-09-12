#!/usr/bin/env python3
"""Apply or revert the temporary Issue #71 suspended-list witness in FreeRTOS tasks.c."""

from __future__ import annotations

import argparse
from pathlib import Path


_TARGET = "            listINSERT_END( &xSuspendedTaskList, &( pxCurrentTCB->xStateListItem ) );"
_STAGE18_19 = "\n".join(
    (
        "            rp86QueueTraceStage( 18U, 0U );",
        _TARGET,
        "            rp86QueueTraceStage( 19U, 0U );",
    )
)
_PATCHED = "\n".join(
    (
        "            rp86QueueTraceStage( 18U, 0U );",
        "            {",
        "                ListItem_t * const pxRp86Index = xSuspendedTaskList.pxIndex;",
        "                rp86QueueTraceStage( 20U, 0U );",
        "",
        "                listTEST_LIST_INTEGRITY( &xSuspendedTaskList );",
        "                listTEST_LIST_ITEM_INTEGRITY( &( pxCurrentTCB->xStateListItem ) );",
        "",
        "                pxCurrentTCB->xStateListItem.pxNext = pxRp86Index;",
        "                rp86QueueTraceStage( 21U, 0U );",
        "                pxCurrentTCB->xStateListItem.pxPrevious = pxRp86Index->pxPrevious;",
        "                rp86QueueTraceStage( 22U, 0U );",
        "                pxRp86Index->pxPrevious->pxNext = &( pxCurrentTCB->xStateListItem );",
        "                rp86QueueTraceStage( 23U, 0U );",
        "                pxRp86Index->pxPrevious = &( pxCurrentTCB->xStateListItem );",
        "                rp86QueueTraceStage( 24U, 0U );",
        "                pxCurrentTCB->xStateListItem.pxContainer = &xSuspendedTaskList;",
        "                rp86QueueTraceStage( 25U, 0U );",
        "                xSuspendedTaskList.uxNumberOfItems =",
        "                    ( UBaseType_t ) ( xSuspendedTaskList.uxNumberOfItems + 1U );",
        "                rp86QueueTraceStage( 26U,",
        "                                     ( unsigned short ) xSuspendedTaskList.uxNumberOfItems );",
        "            }",
        "            rp86QueueTraceStage( 19U, 0U );",
    )
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Apply or revert the temporary Issue #71 trace within the "
            "portMAX_DELAY suspended-list insertion in FreeRTOS tasks.c."
        )
    )
    parser.add_argument(
        "--revert",
        action="store_true",
        help="remove the temporary Stage 18-26 witness",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    tasks_path = repo_root / "third_party" / "FreeRTOS-Kernel" / "tasks.c"
    if not tasks_path.is_file():
        raise SystemExit(
            "FreeRTOS tasks.c is missing; initialize third_party/FreeRTOS-Kernel first"
        )

    source = tasks_path.read_text(encoding="utf-8")

    if args.revert:
        if _PATCHED in source:
            source = source.replace(_PATCHED, _TARGET, 1)
            tasks_path.write_text(source, encoding="utf-8")
            print(f"reverted Issue #71 Stage 18-26 witness: {tasks_path}")
        elif _STAGE18_19 in source:
            source = source.replace(_STAGE18_19, _TARGET, 1)
            tasks_path.write_text(source, encoding="utf-8")
            print(f"reverted Issue #71 Stage 18/19 witness: {tasks_path}")
        elif _TARGET in source:
            print(f"Issue #71 suspended-list witness already absent: {tasks_path}")
        else:
            raise SystemExit("expected suspended-list insertion was not found")
        return 0

    if _PATCHED in source:
        print(f"Issue #71 Stage 18-26 witness already applied: {tasks_path}")
        return 0

    if _STAGE18_19 in source:
        source = source.replace(_STAGE18_19, _PATCHED, 1)
        tasks_path.write_text(source, encoding="utf-8")
        print(f"refined Issue #71 witness from Stage 18/19 to Stage 18-26: {tasks_path}")
        return 0

    occurrences = source.count(_TARGET)
    if occurrences != 1:
        raise SystemExit(
            f"expected exactly one suspended-list insertion, found {occurrences}"
        )

    tasks_path.write_text(source.replace(_TARGET, _PATCHED, 1), encoding="utf-8")
    print(f"applied Issue #71 Stage 18-26 witness: {tasks_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
