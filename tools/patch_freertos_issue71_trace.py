#!/usr/bin/env python3
"""Apply or revert the temporary Issue #71 suspended-list witness in FreeRTOS tasks.c."""

from __future__ import annotations

import argparse
from pathlib import Path


_TARGET = "            listINSERT_END( &xSuspendedTaskList, &( pxCurrentTCB->xStateListItem ) );"
_PATCHED = "\n".join(
    (
        "            rp86QueueTraceStage( 18U, 0U );",
        _TARGET,
        "            rp86QueueTraceStage( 19U, 0U );",
    )
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Apply or revert the temporary Issue #71 trace around the "
            "portMAX_DELAY suspended-list insertion in FreeRTOS tasks.c."
        )
    )
    parser.add_argument(
        "--revert",
        action="store_true",
        help="remove the temporary Stage 18/19 witness",
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
            tasks_path.write_text(source.replace(_PATCHED, _TARGET, 1), encoding="utf-8")
            print(f"reverted Issue #71 Stage 18/19 witness: {tasks_path}")
        elif _TARGET in source:
            print(f"Issue #71 Stage 18/19 witness already absent: {tasks_path}")
        else:
            raise SystemExit("expected suspended-list insertion was not found")
        return 0

    if _PATCHED in source:
        print(f"Issue #71 Stage 18/19 witness already applied: {tasks_path}")
        return 0

    occurrences = source.count(_TARGET)
    if occurrences != 1:
        raise SystemExit(
            f"expected exactly one suspended-list insertion, found {occurrences}"
        )

    tasks_path.write_text(source.replace(_TARGET, _PATCHED, 1), encoding="utf-8")
    print(f"applied Issue #71 Stage 18/19 witness: {tasks_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
