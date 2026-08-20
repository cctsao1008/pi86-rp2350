# Native BIOS Architecture

## Purpose

The Native BIOS is V30 firmware written for the pi86-rp2350 companion-chip
architecture. It starts as a small project-native bring-up BIOS and grows into
the firmware boundary between real-mode software and RP2350-backed services.

It is not initially a copy of the original Pi86 BIOS, nor does the first stage
claim IBM PC compatibility.

## Source layout

```text
firmware/v30/bios/
  native_bios.asm               reset-time payload and bring-up sequence
  include/platform.inc          physical map and companion constants
  include/diagnostic_console.inc  stack-free 00E9h output macros
```

The first image is assembled by repository-pinned NASM 3.02. Includes are
tracked as build dependencies, and the resulting binary is embedded with its
size and SHA-256 identity.

## Current execution contract

The golden-HAT regression maps the compact BIOS payload at physical `F0000h`
and separately supplies the accepted far-jump vector at `FFFF0h`. The payload
uses no RAM or stack, fetches forward until its final `JMP $`, and writes:

```text
PI86 BIOS\r\n
```

to project diagnostic port `00E9h`.

These restrictions keep the payload executable through the permanent
descriptor-fed PC1-C0C0 engine. They are a regression envelope, not the final
BIOS architecture.

## Growth path

1. Complete current-address-indexed C0C1 ROM response.
2. Map a declared BIOS ROM window, including the architectural reset vector.
3. Add minimal internal-SRAM-backed V30 RAM for stack and data.
4. Replace unrolled diagnostics with callable console routines.
5. Add POST status codes and a ROM monitor entry.
6. Define keyboard, display, timer, interrupt, and block-service interfaces.
7. Add PC-compatible interrupt services only after their hardware contracts
   are independently validated.

The agreed PC1-C1 integration contract, including the initial memory map,
Flash image manifest, `INT 10h/AH=0Eh`, structured completion port, and CDC
console separation, is defined in
[`pc1c1_native_bios_platform.md`](pc1c1_native_bios_platform.md).

Until C0C1 is complete, `pc1c_native_bios_foundation` is the physical BIOS
regression and `pc1c_native_bios_hello` remains the earlier immutable HELLO
milestone.

The first foundation payload physically passed on 2026-08-20. See
[`validation/pc1c0c_native_bios_foundation_validation.md`](validation/pc1c0c_native_bios_foundation_validation.md).
