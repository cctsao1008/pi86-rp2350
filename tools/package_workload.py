#!/usr/bin/env python3
"""Build one CRC-protected P86W package from JSON metadata and a flat image."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from rp86_runtime.workload import (  # noqa: E402
    FLAG_CLOCK_FREE_RUNNING,
    FLAG_CLOCK_STEPPED,
    FLAG_PERSISTENT,
    FLAG_SHARED_MEMORY,
    FLAG_STDIO,
    WorkloadManifest,
    encode_workload_file,
    parse_far_pointer,
)


FLAG_NAMES = {
    "persistent": FLAG_PERSISTENT,
    "stdio": FLAG_STDIO,
    "shared-memory": FLAG_SHARED_MEMORY,
}
CLOCK_FLAGS = {
    "auto": 0,
    "free-running": FLAG_CLOCK_FREE_RUNNING,
    "clock-stepped": FLAG_CLOCK_STEPPED,
}


def _number(value: object, field: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ValueError(f"{field} is not an integer: {value!r}") from exc
    raise ValueError(f"{field} must be an integer or numeric string")


def build_package(metadata_path: Path, image_path: Path) -> bytes:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if metadata.get("format") != "p86w-v1":
        raise ValueError("metadata format must be p86w-v1")
    image = image_path.read_bytes()
    entry_segment, entry_offset = parse_far_pointer(metadata["entry"])
    stack_segment, stack_offset = parse_far_pointer(metadata.get("stack", "0000:0000"))

    flags = CLOCK_FLAGS[metadata.get("clock", "auto")]
    for name in metadata.get("flags", []):
        try:
            flags |= FLAG_NAMES[name]
        except KeyError as exc:
            raise ValueError(f"unknown workload flag: {name!r}") from exc

    shared = metadata.get("shared_memory")
    shared_base = shared_size = 0
    if shared is not None:
        shared_base = _number(shared["base"], "shared_memory.base")
        shared_size = _number(shared["size"], "shared_memory.size")
        flags |= FLAG_SHARED_MEMORY

    manifest = WorkloadManifest.for_image(
        image,
        load_address=_number(metadata["load_address"], "load_address"),
        entry_segment=entry_segment,
        entry_offset=entry_offset,
        stack_segment=stack_segment,
        stack_offset=stack_offset,
        shared_base=shared_base,
        shared_size=shared_size,
        flags=flags,
    )
    return encode_workload_file(manifest, image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    package = build_package(args.metadata, args.image)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(f"P86W {args.output.name}: {len(package)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
