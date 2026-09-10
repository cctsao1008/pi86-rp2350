# Processor C/16 ABI

Status: **provisional for Issue #58**. The compile/link/package ABI is now CI-proven; this document becomes fully accepted only after physical Intel 8086 execution validates the generated image.

## Scope

This contract defines the first C toolchain boundary for programs executed natively by the physical Intel 8086 / NEC V30. It does not change the Host-managed RP86 architecture, the processor workload lifecycle, or the existing NASM-only image path.

The first objective is deliberately narrow: freestanding 16-bit C and NASM must link into one native image, package through the existing `.P86W` path, load at the normal processor workload base, and execute without DOS or a BIOS.

## Toolchain baseline

| Item | Issue #58 baseline |
|---|---|
| C compiler | Open Watcom C/16 (`wcc`) |
| Linker | Open Watcom Linker (`wlink`) |
| Assembly | NASM, 16-bit OMF `obj` output |
| CPU baseline | Intel 8086 common baseline |
| C CPU switch | `-0` |
| Initial memory model | `-ms` small code / small data |
| Stack assumption | `SS` may differ from `DGROUP` (`-zu`) |
| Stack checking | disabled (`-s`) until an RP86-owned check exists |
| Default-library records | suppressed (`-zl`) |
| Compiler target selector | `-bt=dos`; selects the 16-bit compiler/object convention only |
| C object format | 16-bit OMF |
| NASM object format | `-f obj` |
| Link relocation model | WLINK 16-bit segmented DOS format, with default libraries disabled |
| Emitted image | `OUTPUT RAW OFFSET=0x10000` |
| Package | existing `.P86W` packager |

The historical FreeRTOS Open Watcom donor used the large memory model. RP86 starts with the small model because the first port should minimize segmentation surface area while the kernel, heap, TCBs, task stacks, and C data fit inside one 64 KiB data group. Large/far-data models remain a later option rather than an assumption inherited from the DOS port.

## Reproducible compiler input

CI does not use the mutable Open Watcom `Current-build` or `Last-CI-build` aliases. Issue #58 pins the named release artifact:

```text
Open Watcom release: 2026-09-01-Build
source tag object:   7229ffe0f1e93da1966be6c10f01f6a96077a6f0
source commit:       39349919cd52aa0fb37b896c79b1331f1dc00b7e
asset:               ow-snapshot.tar.xz
SHA-256:             bac354f3c75ffa49ff8d70a44e475de7e7c1823fff04b80c14787bd0792c9bdf
```

The CI job downloads that exact named asset and rejects it unless the SHA-256 digest matches before extraction. This makes the compiler binary input reproducible even if a mutable convenience alias later changes.

## Initial memory and pointer model

The first C/16 ABI uses the Open Watcom small model:

- code is limited to one 64 KiB code segment;
- ordinary data pointers are 16-bit offsets relative to `DS` / `DGROUP`;
- ordinary function pointers are near 16-bit code offsets;
- C static data, the initial FreeRTOS heap, TCBs, and task stacks must fit inside the selected 64 KiB C data group;
- `SS` is not assumed equal to `DS`;
- the 256 KiB processor-visible Internal-SRAM backing is **not** treated as one flat C address space.

This is an implementation constraint, not an RP86 memory-map change. Native assembly workloads remain free to use the wider processor-visible address space directly.

## Entry and register contract

The `.P86W` reset handoff enters a project-owned NASM startup stub at the workload entry point. The startup stub preserves the RP86-provided `SS:SP`, executes `CLI` and `CLD`, loads `DS` and `ES` with the linked C `DGROUP`, deterministically clears C BSS, and then calls the explicitly declared `__cdecl` C entry point. The 16-bit C return value is consumed in `AX`, reported through the existing RP86 processor I/O ABI, and the validation workload terminates through the existing `IDLE_PREPARE` + `HLT` contract.

The C/assembly boundary does not depend on a compiler-generated interrupt frame. FreeRTOS context switching remains project-owned assembly work under #60.

## Link and load-address model

RP86 copies workload byte zero to the manifest `load_address`. The initial C/16 workload uses physical load address `0x10000` and entry `1000:0000`.

Segment-valued OMF fixups require a segmented linker model. WLINK `FORMAT RAW` is not used for linking because its flat-memory model rejects 8086 segment relocations. Instead, Issue #58 links with normal 16-bit segmented relocation semantics and overrides only the emitted representation:

```text
FORMAT DOS
OPTION NODEFAULTLIBS
OPTION START=rp86_c16_entry
ORDER CLNAME CODE SEGADDR=0x1000 SEGMENT _TEXT CLNAME DATA CLNAME BSS
OUTPUT RAW OFFSET=0x10000
```

`ORDER ... SEGADDR=0x1000` fixes the code class at the RP86 physical workload segment. `OUTPUT RAW OFFSET=0x10000` omits physical-address padding from the emitted binary without changing the linker's address calculations. `FORMAT DOS` is therefore a **link-time segmented relocation model**, not a DOS runtime dependency; no DOS executable loader, BIOS service, DOS interrupt, or default C runtime library is present in the RP86 execution path.

CI evidence currently resolves the smoke workload as:

```text
entry:              1000:0000
_TEXT:              1000:0000
DGROUP:             1007:0000
_rp86_data_anchor:  1007:0000
_rp86_bss_probe:    1007:0010
_BSS physical:      1008:0000, size 0x0002
```

The generated startup correspondingly loads `DS=ES=0x1007`, demonstrating that OMF segment relocation and the RP86 physical load base agree.

## BSS policy

Zero-initialized C storage must not depend on the previous contents of RP2350 SRAM or a loader buffer. WLINK provides linker-defined `_edata` and `_end` bounds for the BSS class. With `DS=ES=DGROUP`, the startup clears the half-open byte range `[_edata, _end)` before entering C:

```asm
mov di, _edata
mov cx, _end
sub cx, di
xor ax, ax
rep stosb
```

CI cross-checks the resolved immediates in the raw startup against the WLINK map-derived BSS range. In the current smoke image the linker resolves the range to `DGROUP:+0x0010 .. +0x0012`, exactly two bytes. The C validation entry checks `rp86_bss_probe == 0` before first use; it returns `0xB551` if that contract is violated. The normal expected result remains `0x147A`.

This is structural/link-time proof of the initialization mechanism. Physical execution is still required before Issue #58 can claim the runtime behavior is validated on Intel 8086 hardware.

## Freestanding rule

Processor-side C in this stage has no hosted C startup and may not assume DOS services. In particular, there are no `_dos_*` APIs, 8254/8259 programming, hosted `main()` startup, or standard-library calls unless a specific implementation is deliberately provided and audited for the Intel 8086 baseline. Compiler-generated helper calls are also part of the audit surface.

The first smoke workload deliberately uses simple 16-bit integer operations, globals, a BSS object, a pointer, a C function call, local stack use, and a 16-bit return value so the generated instruction surface remains auditable.

## Current CI evidence

The C16 validation workflow currently proves all of the following in one processor-only build: Open Watcom C/16 compilation, NASM OMF assembly, mixed OMF linking, fixed physical entry `1000:0000`, relocated `DGROUP`, linker-derived BSS clearing, raw binary generation, `.P86W` packaging, and rejection of an explicit set of obvious post-8086 context opcodes.

The WLINK warning `W1014: stack segment not found` is expected for this workload. RP86 owns initial `SS:SP` through the workload manifest/reset handoff, and the C compiler is built with `-zu`; the linker is not asked to allocate a DOS stack segment.

## Required verification before closing #58

- [x] reproducible Open Watcom C/16 binary input is pinned and digest-checked;
- [x] mixed Open Watcom C OMF + NASM OMF links without DOS/CRT libraries;
- [x] WLINK map proves code/data/group placement compatible with the RP86 physical load base;
- [x] raw binary byte zero corresponds to physical `0x10000` while segment fixups retain physical addressing;
- [x] deterministic BSS clearing is defined from linker-derived bounds and checked against the linked image;
- [x] initialized data, locals, calls, globals, pointers, and stack use are represented by the smoke workload;
- [x] the image is packaged by the existing `.P86W` path;
- [ ] execute the validation workload on a physical Intel 8086 and observe result `0x147A`;
- [ ] confirm NEC V30 compatibility on the same C/16 ABI path.

The CI opcode scan is a useful guard, not a mathematical proof that every emitted byte is valid 8086 code. Physical Intel 8086 execution remains the decisive CPU-baseline gate.

## Open items after #58

The initial `-ms` memory model remains subject to FreeRTOS kernel/heap sizing. The RTOS interrupt/tick ABI and exact task context are intentionally outside this toolchain issue and remain follow-on processor-port work.

Related: #57, #58, #60, #62.
