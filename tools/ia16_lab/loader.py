"""Load production RP86 P86W artifacts for software execution."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from tools.rp86_runtime.workload import WorkloadManifest, decode_workload_file


@dataclass(frozen=True)
class LoadedWorkload:
    """Decoded production workload artifact."""

    path: Path
    manifest: WorkloadManifest
    image: bytes

    @property
    def entry_linear(self) -> int:
        return linear_address(
            self.manifest.entry_segment,
            self.manifest.entry_offset,
        )

    @property
    def stack_linear(self) -> int:
        return linear_address(
            self.manifest.stack_segment,
            self.manifest.stack_offset,
        )


def linear_address(segment: int, offset: int) -> int:
    """Translate one real-mode segment:offset pair into a 20-bit address."""
    if not 0 <= segment <= 0xFFFF or not 0 <= offset <= 0xFFFF:
        raise ValueError("segment and offset must be 16-bit values")
    address = (segment << 4) + offset
    if address >= 0x100000:
        raise ValueError("segment:offset is outside the 20-bit address space")
    return address


def load_p86w(path: str | Path) -> LoadedWorkload:
    """Decode an existing P86W using the canonical RP86 workload parser."""
    source = Path(path)
    manifest, image = decode_workload_file(source.read_bytes())
    return LoadedWorkload(source, manifest, image)
