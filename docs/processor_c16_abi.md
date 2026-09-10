# Processor C/16 ABI

Status: **provisional for Issue #58**. This document becomes authoritative only after the mixed C + NASM build and physical Intel 8086 validation gates pass.

## Scope

This contract defines the first C toolchain boundary for programs executed natively by the physical Intel 8086 / NEC V30. It does not change the Host-managed RP86 architecture, the processor workload lifecycle, or the existing NASM-only image path.

The first objective is deliberately narrow: prove that freestanding 16-bit C and NASM can be linked into the same native image, packaged as `.P86W`, loaded at the normal processor workload base, and executed without DOS or a BIOS.

## Toolchain candidate

| Item | Provisional decision |
|---|---|
| C compiler | Open Watcom C/16 (`wcc`) |
| Linker | Open Watcom Linker (`wlink`) |
| Assembly | NASM, OMF `obj` output |
| CPU baseline | Intel 8086 common baseline |
| C CPU switch | `-0` |
| Initial memory model | `-ms` small code / small data |
| Stack assumption | `SS` may differ from `DGROUP` (`-zu`) |
| Stack checking | disabled (`-s`) until an RP86-owned check exists |
| Default-library records | suppressed (`-zl`) |
| Target environment selector | `-bt=dos`, used only for the 16-bit object/toolchain convention; no DOS runtime is linked or called |
| C object format | 16-bit OMF |
| NASM object format | `-f obj` (16-bit OMF) |
| Image format | WLINK raw binary, then existing `.P86W` packager |

The historical FreeRTOS Open Watcom donor used the large memory model. RP86 intentionally starts with the smaller model because the first port should minimize segmentation surface area while the kernel, heap, task stacks, and C data all fit inside one 64 KiB data group. Large/far-data models remain a later option rather than an assumption inherited from the DOS port.

## Initial memory and pointer model

The first C/16 ABI uses the Open Watcom small model:

- code is limited to one 64 KiB code segment;
- ordinary data pointers are 16-bit offsets relative to `DS` / `DGROUP`;
- ordinary function pointers are near 16-bit code offsets;
- C static data, the initial FreeRTOS heap, TCBs, and task stacks must fit inside the selected 64 KiB C data group;
- `SS` is not assumed equal to `DS`; compiler generation must remain valid with RP86-provided stacks;
- the 256 KiB processor-visible Internal-SRAM backing is **not** treated as one flat C address space.

This is an implementation constraint, not an RP86 memory-map change. Other native assembly workloads remain free to use the wider processor-visible address space directly.

## Entry and register contract

The `.P86W` reset handoff enters a project-owned NASM startup stub at the workload entry point. That stub owns the transition into C:

1. preserve the RP86-provided `SS:SP`;
2. execute `CLD`;
3. initialize `DS` and `ES` to the linked C `DGROUP`;
4. call the C validation entry point using an explicitly declared C calling convention;
5. consume the C return value in `AX`;
6. report the result through the existing RP86 processor I/O ABI;
7. use the existing `IDLE_PREPARE` + `HLT` terminal-completion contract.

The C/assembly boundary must not depend on a compiler-generated interrupt frame. FreeRTOS context switching remains project-owned NASM work under #60.

## Link and load-address model

RP86 copies workload byte zero to the manifest `load_address`. The initial C/16 workload uses the existing default physical load address `0x10000` and entry `1000:0000`.

For segmented/fixup-aware linking, the linker must calculate addresses as if the image begins at physical `0x10000`; simply linking at zero and copying the file to `0x10000` is not sufficient once segment-valued relocations exist.

The Issue #58 implementation therefore evaluates this WLINK raw-image model:

```text
FORMAT RAW
OPTION OFFSET=0x10000
OUTPUT RAW OFFSET=0x10000
```

`OPTION OFFSET` establishes the linked raw-image base; `OUTPUT RAW OFFSET` is intended to omit the leading load-address padding from the emitted file. This exact combination remains provisional until the linker map and generated binary demonstrate the expected `1000h`-based segment values and RP86 load semantics.

## Freestanding rule

Processor-side C in this stage has no C runtime startup and may not assume DOS services. In particular:

- no `_dos_*` vector or filesystem APIs;
- no 8254/8259 programming;
- no `setjmp`/`longjmp` scheduler-return path;
- no implicit hosted `main()` startup;
- no standard-library function may be used unless its implementation is deliberately provided and audited for the 8086 baseline.

Compiler-generated helper calls are also part of the audit surface. The first smoke workload uses simple 16-bit integer operations to keep that surface small.

## BSS policy

Zero-initialized C storage must not be accepted merely because a simulator, loader buffer, or previous run happened to contain zeroes. Before #58 closes, the startup/link contract must explicitly prove how `_BSS` is bounded and zeroed, or otherwise prove an equivalent deterministic initialization mechanism.

The first compile/link smoke may precede that proof, but physical acceptance may not.

## Required verification before closing #58

The following evidence is required:

- `wcc` reports/uses the intended 16-bit toolchain and `-0` CPU target;
- mixed Open Watcom C OMF + NASM OMF links without a DOS/CRT library;
- WLINK map shows code/data/group placement compatible with `docs/processor_memory_map.md`;
- raw binary begins at workload byte zero while segment fixups correspond to physical base `0x10000`;
- disassembly contains no 80186+ or NEC-only instruction in the common baseline path;
- initialized data and deterministic zero-initialized data behave correctly;
- C locals, calls, globals, pointers, and stack use execute correctly;
- the image is packaged by the existing `.P86W` path;
- the validation workload executes on a physical Intel 8086; NEC V30 compatibility is preserved.

## Open items

Until measured/build evidence resolves them, the following remain provisional rather than silently assumed:

- exact Open Watcom release/commit used as the reproducible compiler pin;
- exact C symbol decoration at the NASM boundary;
- exact `DGROUP` and BSS linker symbols used by the startup stub;
- whether `-ms` remains the FreeRTOS v1 memory model after the kernel-size/heap budget is measured;
- final linker directives for compact raw output at physical base `0x10000`.

Related: #57, #58, #60, #62.
