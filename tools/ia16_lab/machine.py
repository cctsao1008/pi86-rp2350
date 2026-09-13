"""Minimal x86-16 execution backend for RP86 workload images."""

from __future__ import annotations

from dataclasses import dataclass

from .loader import LoadedWorkload, linear_address


class UnicornUnavailable(RuntimeError):
    """Raised when the optional Unicorn dependency is not installed."""


@dataclass(frozen=True)
class RegisterState:
    ax: int
    bx: int
    cx: int
    dx: int
    si: int
    di: int
    bp: int
    sp: int
    cs: int
    ds: int
    es: int
    ss: int
    ip: int
    flags: int


class IA16Machine:
    """Execute a production IA16 image under a small observable CPU model.

    This class deliberately models only processor-visible memory/register state.
    It does not model RP2350, PIO, DMA, bus timing, or strict Intel-8086 ISA
    legality.  ISA legality remains a separate validation concern.
    """

    PAGE_SIZE = 0x1000
    DEFAULT_MEMORY_SIZE = 0x40000

    def __init__(self, *, memory_size: int = DEFAULT_MEMORY_SIZE) -> None:
        if memory_size <= 0 or memory_size % self.PAGE_SIZE:
            raise ValueError("memory_size must be a positive 4 KiB multiple")

        try:
            from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
            from unicorn.x86_const import (
                UC_X86_REG_AX,
                UC_X86_REG_BP,
                UC_X86_REG_BX,
                UC_X86_REG_CS,
                UC_X86_REG_CX,
                UC_X86_REG_DI,
                UC_X86_REG_DS,
                UC_X86_REG_DX,
                UC_X86_REG_EFLAGS,
                UC_X86_REG_ES,
                UC_X86_REG_IP,
                UC_X86_REG_SI,
                UC_X86_REG_SP,
                UC_X86_REG_SS,
            )
        except ImportError as exc:
            raise UnicornUnavailable(
                "Unicorn is required for IA16 execution; install "
                "tools/ia16_lab/requirements.txt"
            ) from exc

        self._register_ids = {
            "ax": UC_X86_REG_AX,
            "bx": UC_X86_REG_BX,
            "cx": UC_X86_REG_CX,
            "dx": UC_X86_REG_DX,
            "si": UC_X86_REG_SI,
            "di": UC_X86_REG_DI,
            "bp": UC_X86_REG_BP,
            "sp": UC_X86_REG_SP,
            "cs": UC_X86_REG_CS,
            "ds": UC_X86_REG_DS,
            "es": UC_X86_REG_ES,
            "ss": UC_X86_REG_SS,
            "ip": UC_X86_REG_IP,
            "flags": UC_X86_REG_EFLAGS,
        }
        self._uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self._uc.mem_map(0, memory_size)
        self.memory_size = memory_size
        self.workload: LoadedWorkload | None = None

    def load(self, workload: LoadedWorkload) -> None:
        """Load one decoded production workload and apply manifest CPU state."""
        manifest = workload.manifest
        image_end = manifest.load_address + len(workload.image)
        if image_end > self.memory_size:
            raise ValueError(
                f"workload image ends at 0x{image_end:05X}, outside modeled "
                f"memory 0x00000-0x{self.memory_size - 1:05X}"
            )
        if workload.stack_linear >= self.memory_size:
            raise ValueError("manifest stack lies outside modeled memory")

        self._uc.mem_write(manifest.load_address, workload.image)
        self._write_reg("cs", manifest.entry_segment)
        self._write_reg("ip", manifest.entry_offset)
        self._write_reg("ss", manifest.stack_segment)
        self._write_reg("sp", manifest.stack_offset)
        self.workload = workload

    def run(self, *, instruction_count: int) -> None:
        """Execute at most ``instruction_count`` instructions from current CS:IP."""
        if self.workload is None:
            raise RuntimeError("no workload is loaded")
        if instruction_count <= 0:
            raise ValueError("instruction_count must be positive")

        start = self.current_linear_ip()
        self._uc.emu_start(
            start,
            self.memory_size,
            timeout=0,
            count=instruction_count,
        )

    def current_linear_ip(self) -> int:
        state = self.registers()
        return linear_address(state.cs, state.ip)

    def registers(self) -> RegisterState:
        values = {
            name: int(self._uc.reg_read(register_id))
            for name, register_id in self._register_ids.items()
        }
        return RegisterState(**values)

    def read_memory(self, address: int, size: int) -> bytes:
        if address < 0 or size < 0 or address + size > self.memory_size:
            raise ValueError("memory read is outside modeled memory")
        return bytes(self._uc.mem_read(address, size))

    def write_memory(self, address: int, data: bytes) -> None:
        if address < 0 or address + len(data) > self.memory_size:
            raise ValueError("memory write is outside modeled memory")
        self._uc.mem_write(address, data)

    def _write_reg(self, name: str, value: int) -> None:
        self._uc.reg_write(self._register_ids[name], value)
