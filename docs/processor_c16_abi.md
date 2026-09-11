# Processor C/16 ABI

Status: **Intel 8086 physically validated for Issue #58; NEC V30 compatibility remains pending**. The compile/link/package ABI is CI-proven and the generated validation image has passed the RP86 physical-regression path on a real Intel 8086.

## Scope

This contract defines the first C toolchain boundary for programs executed natively by the physical Intel 8086 / NEC V30. It does not change the Host-managed RP86 architecture, the processor workload lifecycle, or the existing NASM-only image path.

The first objective is deliberately narrow: freestanding 16-bit C and NASM must link into one native image, package through the existing `.P86W` path, load at the normal processor workload base, and execute without DOS or a BIOS.

## Issue #58 ABI decision record

This table is the authoritative C/16 ABI handoff required by Issue #58 and consumed by the later FreeRTOS port.

| Required decision | RP86 C/16 contract |
|---|---|
| Compiler | Open Watcom C/16, `wcc` |
| Compiler version | Open Watcom C x86 16-bit Optimizing Compiler 2.0 beta, Sep 1 2026 05:44:21 |
| Linker | Open Watcom Linker 2.0 beta, Sep 1 2026 05:40:41 |
| CPU target / code-generation switches | `-0 -ms -ecc -zu -s -zl -zq -bt=dos`; `-0` is the Intel 8086 baseline and `-ecc` makes `__cdecl` the default calling convention |
| Memory model | `-ms`, small code / small data |
| Code pointer width / near-far rule | ordinary code pointers are near 16-bit offsets in the single code segment; explicit far code pointers/calls are outside the v1 contract |
| Data pointer width / near-far rule | ordinary data pointers are near 16-bit offsets relative to `DS` / `DGROUP`; explicit far data pointers are outside the v1 contract |
| Function pointer representation | near 16-bit code offset under the selected small model |
| Calling convention | project default is 16-bit `__cdecl`: arguments on the stack, caller removes arguments, ordinary 16-bit scalar return in `AX`; assembly-visible entry points are still annotated explicitly |
| Register preservation rules | `AX`, `BX`, `CX`, `DX` are caller-clobbered; `BP`, `SI`, `DI` are callee-preserved. RP86 additionally requires C callees to preserve the established `DS=DGROUP` and startup-established `ES=DGROUP` convention. `SS` is not changed by an ordinary C call; `SP` returns to the call-site value before caller argument cleanup |
| DS convention | `DS=DGROUP` before entering C and for the lifetime of ordinary small-model C execution |
| ES convention | startup initializes `ES=DGROUP`; processor-side C/assembly glue treats it as preserved unless a narrowly documented primitive says otherwise |
| SS relation to DS | independent; compiled with `-zu`, so no `SS==DS` assumption is permitted |
| Stack model and maximum practical task stack | 16-bit `SS:SP`. A single stack has at most 64 KiB offset addressability. For the initial FreeRTOS small-model path, C-owned task stack buffers must remain representable by near pointers and therefore share the 64 KiB `DGROUP` budget unless #60 explicitly adds segment-aware stack metadata. No larger/far task-stack model is authorized by #58 |
| Heap addressability / segment ownership | the initial C heap is a near-data region owned inside `DGROUP`; kernel data, heap, TCBs, and C-addressable task stack storage share the 64 KiB data-group budget. The wider 256 KiB processor backing and future PSRAM are not automatically C heap space |
| Object format | 16-bit OMF |
| NASM interoperability path | NASM `-f obj` emits 16-bit OMF; NASM startup/port objects and Open Watcom C objects are linked together by WLINK |
| Linked-image format | WLINK 16-bit segmented image model with `_TEXT` fixed at physical `0x10000` / `1000:0000` |
| Flat-binary conversion path | WLINK `OUTPUT RAW OFFSET=0x10000`; byte zero of the emitted file corresponds to physical `0x10000` while segment fixups retain physical link addresses |
| 8086 ISA verification method | compile with `-0`; retain WLINK map; disassemble only the linked `_TEXT` range; reject an explicit set of obvious post-8086 opcodes in CI; final CPU-baseline acceptance is execution on a physical Intel 8086 |

Open Watcom's 16-bit `__cdecl` convention declares `AX/BX/CX/DX` as modified, uses stack arguments with caller cleanup, and returns ordinary scalar values through `AX`. The smoke workload deliberately leaves its helper function unannotated so CI exercises the project-wide `-ecc` default rather than only explicit per-function modifiers.

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
- C static data, the initial FreeRTOS heap, TCBs, and C-addressable task stack storage share the selected 64 KiB C data group;
- `SS` is not assumed equal to `DS`;
- the 256 KiB processor-visible Internal-SRAM backing is **not** treated as one flat C address space.

This is an implementation constraint, not an RP86 memory-map change. Native assembly workloads remain free to use the wider processor-visible address space directly.

## Entry and register contract

The `.P86W` reset handoff enters a project-owned NASM startup stub at the workload entry point. The startup stub preserves the RP86-provided `SS:SP`, executes `CLI` and `CLD`, loads `DS` and `ES` with the linked C `DGROUP`, deterministically clears C BSS, and then calls the C entry point. The 16-bit C return value is published through `RP86_IO_PORT_RESULT` before the startup converts the expected-checksum contract into RP86's formal diagnostic PASS/FAIL line and terminates through the existing `IDLE_PREPARE` + `HLT` contract.

All processor C translation units use `-ecc`, making `__cdecl` the default. Assembly-facing functions should still spell out `__cdecl` in their declarations as executable interface documentation. The C/assembly boundary does not depend on a compiler-generated interrupt frame. FreeRTOS context switching remains project-owned assembly work under #60.

The boot workload may enter with an `SS` that differs from `DGROUP`; the smoke manifest uses `2000:FFF0`. That is valid because of `-zu`. It does **not** imply that an ordinary near C pointer can identify arbitrary future stack segments. The FreeRTOS port must respect the near-pointer limit when representing task stack storage, or explicitly extend the port with segment-aware metadata.

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

CI run #16 resolves the current smoke image as:

```text
entry:              1000:0000
_TEXT:              1000:0000, size 0x00B5
DGROUP:             100C:0000
_rp86_c16_add:      1000:0087
_rp86_c16_main:     1000:0092
_rp86_data_anchor:  100C:0000
_rp86_bss_probe:    100C:0010
_BSS physical:      100D:0000, size 0x0002
raw image:          194 bytes
C16SMOKE.P86W:      234 bytes
```

The generated startup correspondingly loads `DS=ES=0x100C`, demonstrating that OMF segment relocation and the RP86 physical load base agree.

## BSS policy

Zero-initialized C storage must not depend on the previous contents of RP2350 SRAM or a loader buffer. WLINK provides linker-defined `_edata` and `_end` bounds for the BSS class. With `DS=ES=DGROUP`, the startup clears the half-open byte range `[_edata, _end)` before entering C:

```asm
mov di, _edata
mov cx, _end
sub cx, di
xor ax, ax
rep stosb
```

CI cross-checks the resolved immediates in the raw startup against the WLINK map-derived BSS range. In the current smoke image the linker resolves the range to `DGROUP:+0x0010 .. +0x0012`, exactly two bytes. The C validation entry checks `rp86_bss_probe == 0` before first use; it returns `0xB551` if that contract is violated.

## Native validation result contract

Normal C execution computes:

```text
0x1357 + 0x0022 + 0x0101 = 0x147A
```

The startup always publishes the returned `AX` value to `RP86_IO_PORT_RESULT`. It emits the exact RP86 acceptance line `RESULT: PASS` only when `AX == 0x147A`; every other value, including the BSS-failure sentinel `0xB551`, emits `RESULT: FAIL`. It then arms terminal idle and reaches `HLT`.

This makes `tools/rp86.py --physical-regression C16SMOKE.P86W` suitable as the physical acceptance path: successful lifecycle completion alone is not enough; the firmware-owned formal PASS flag must also have been produced by the native checksum comparison.

## Physical Intel 8086 evidence

On 2026-09-11, the validation workload passed the physical RP86 regression path on an Intel 8086 using the Issue #58 branch image without a processor-specific rebuild:

```text
processor:           INTEL 8086
image size:          194 bytes
load address:        0x10000
entry:               1000:0000
clock:               CLOCK-STEPPED
CRC32:               89E30ABB
result:              PASS
completion reason:   NATIVE_HLT
cycles:              121
processor signature: 0012
native output:       RESULT: PASS
physical regression: PASS
```

Because startup emits `RESULT: PASS` only after the native return value satisfies `AX == 0x147A`, this run physically validates the complete C/16 execution path exercised by the smoke workload: reset handoff, `SS:SP`, `DGROUP`, BSS initialization, initialized and writable data, near C calls, default `__cdecl` caller cleanup, return value in `AX`, RP86 result publication, and terminal lifecycle completion.

The remaining compatibility gate is to execute the **same `C16SMOKE.P86W` package** on a physical NEC V30. It must not be rebuilt with a V30-specific target.

## Freestanding rule

Processor-side C in this stage has no hosted C startup and may not assume DOS services. In particular, there are no `_dos_*` APIs, 8254/8259 programming, hosted `main()` startup, or standard-library calls unless a specific implementation is deliberately provided and audited for the Intel 8086 baseline. Compiler-generated helper calls are also part of the audit surface.

The first smoke workload deliberately uses simple 16-bit integer operations, initialized data, BSS, globals, a near pointer, an unannotated default-`__cdecl` C function call, local stack use, and a 16-bit return value so the generated instruction surface remains auditable.

## Current CI evidence

Processor C16 ABI run #16 is green. The processor-only workflow proves pinned Open Watcom C/16 acquisition with SHA-256 verification, NASM OMF assembly, mixed OMF linking, fixed physical entry `1000:0000`, relocated `DGROUP`, linker-derived BSS clearing, raw binary generation, `.P86W` packaging, and an opcode scan constrained to the executable `_TEXT` range rather than interpreting DATA/BSS bytes as code.

The linked executable visibly contains `cmp ax,0x147a` and separate native `RESULT: PASS` / `RESULT: FAIL` output paths before terminal completion. The unannotated C helper is called with two pushed 16-bit arguments followed by caller-side `add sp,4`, matching the project-wide `-ecc` / `__cdecl` decision.

The WLINK warning `W1014: stack segment not found` is expected for this workload. RP86 owns initial `SS:SP` through the workload manifest/reset handoff, and the C compiler is built with `-zu`; the linker is not asked to allocate a DOS stack segment.

## Required verification before closing #58

- [x] reproducible Open Watcom C/16 binary input is pinned and digest-checked;
- [x] complete C/16 ABI decision record is populated;
- [x] `__cdecl` is the project-wide compiler default and exercised by an unannotated C helper;
- [x] mixed Open Watcom C OMF + NASM OMF links without DOS/CRT libraries;
- [x] WLINK map proves code/data/group placement compatible with the RP86 physical load base;
- [x] raw binary byte zero corresponds to physical `0x10000` while segment fixups retain physical addressing;
- [x] deterministic BSS clearing is defined from linker-derived bounds and checked against the linked image;
- [x] initialized data, locals, calls, globals, pointers, and stack use are represented by the smoke workload;
- [x] the image is packaged by the existing `.P86W` path;
- [x] CI ISA auditing is restricted to the linked executable `_TEXT` bytes;
- [x] native checksum success is converted into RP86's formal `RESULT: PASS` acceptance signal;
- [x] execute the validation workload on a physical Intel 8086 and observe the `AX == 0x147A` acceptance predicate with formal PASS;
- [ ] confirm NEC V30 compatibility on the same C/16 ABI path.

The CI opcode scan is a useful guard, not a mathematical proof that every emitted instruction is valid on every 8086 implementation. The physical Intel 8086 regression above is the decisive CPU-baseline evidence for this workload.

## Open items after #58

The initial `-ms` memory model remains subject to FreeRTOS kernel/heap sizing. The RTOS interrupt/tick ABI and exact task context are intentionally outside this toolchain issue and remain follow-on processor-port work. In particular, #60 must choose the task-stack segment representation without weakening the near-pointer rules established here.

Related: #57, #58, #60, #62.
