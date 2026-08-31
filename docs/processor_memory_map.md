# Physical Processor Memory Map

This document defines the canonical processor-visible memory map for the real
Intel 8086 and NEC V30 used by RP86. It describes physical processor addresses,
not RP2350 addresses, Host virtual memory, the NOR flash layout, or filesystem
paths.

The currently validated general-execution backing is RP2350 Internal SRAM.
External PSRAM is an optional future capacity tier and does not yet own a
processor address range.

## Address formation

The Intel 8086 and NEC V30 expose a 20-bit physical address bus:

```text
physical = ((segment << 4) + offset) & 0xFFFFF
```

The physical address space is therefore exactly 1 MiB:

```text
00000h-FFFFFh
```

More than one `segment:offset` pair can name the same physical byte. RP86 Host
commands and workload manifests use physical addresses wherever possible to
avoid segment-alias ambiguity. For example:

```text
1000:0000 -> physical 10000h
0FFF:0010 -> physical 10000h
3000:F000 -> physical 3F000h
FFFF:0000 -> physical FFFF0h (architectural RESET location)
```

Address arithmetic that carries beyond bit 19 wraps on the physical 20-bit
bus. Workloads should not depend on this wrap unless it is an intentional part
of the test.

## Canonical map

| Physical range | Size | Current role | Backing and status |
|---|---:|---|---|
| `00000h-0FFFFh` | 64 KiB | Low memory available to the workload | Internal SRAM; implemented and physically validated |
| `10000h-3EFFFh` | 188 KiB | Principal general-workload image, data, and optional stack area | Internal SRAM; implemented and physically validated |
| `3F000h-3FFFFh` | 4 KiB | Host/processor shared mailbox | Internal SRAM; reserved, implemented, and physically validated |
| `40000h-EFFFFh` | 704 KiB | Unassigned processor address space | Unmapped; no backing is claimed |
| `F0000h-FFFEFh` | 65,520 bytes | Prepared diagnostic ROM only | Not mapped for general workloads |
| `FFFF0h-FFFFFh` | 16 bytes | RESET handoff window | RP2350-served handoff for general workloads; part of the prepared ROM in diagnostic mode |

```text
FFFFF  +--------------------------------------------------+
       | RESET handoff                              16 B  | FFFF0-FFFFF
FFFF0  +--------------------------------------------------+
       | Prepared diagnostic ROM window                   |
       | General workload mode: unmapped                  | F0000-FFFEF
F0000  +--------------------------------------------------+
       |                                                  |
       | Unassigned / unmapped processor space            |
       | No PSRAM mapping is claimed yet                  | 40000-EFFFF
       |                                                  |
40000  +--------------------------------------------------+
       | Shared mailbox                              4 KiB | 3F000-3FFFF
3F000  +--------------------------------------------------+
       | General workload image/data/stack area     188 KiB| 10000-3EFFF
10000  +--------------------------------------------------+
       | Low memory                                 64 KiB | 00000-0FFFF
00000  +--------------------------------------------------+
```

The first three rows are one contiguous 256 KiB Internal-SRAM backing:

```text
00000h-3FFFFh = 0x40000 bytes = 256 KiB
```

The mailbox is inside that backing but is reserved by the runtime contract.

## Low memory: `00000h-0FFFFh`

RP86 does not impose a PC-compatible low-memory layout. This region may contain
workload code, data, an interrupt vector table, or other workload-defined state.

The conventional 8086 interrupt-vector-table location is available:

```text
00000h-003FFh = 256 vectors x 4 bytes
```

That convention is not automatically populated for every workload. A workload
that enables interrupts must install the vectors and handlers it needs, or use
a runtime service that explicitly provides them. BIOS data areas, DOS memory,
video RAM, and PC option-ROM conventions are not implicit parts of RP86.

## General workload region: `10000h-3EFFFh`

The default flat workload placement is:

```text
load physical address = 10000h
entry                  = 1000:0000
```

The current examples use this placement:

- `HELLO.P86W`
- `CALC.P86W`
- `MAILBOX.P86W`
- `INVSQRT.P86W`

This default is a convention, not a hard-coded requirement. A manifest may
select another address inside the active backing if all validation rules are
satisfied.

### Workload manifest validation

Before accepting an image, both Host tooling and firmware enforce these rules:

1. The image is non-empty.
2. `load_address + image_size` does not overflow the 20-bit address space.
3. The complete image is inside the selected available backing.
4. The image does not overlap `3F000h-3FFFFh`.
5. The physical entry address lies inside the uploaded image.
6. An explicitly requested stack lies in writable backing below `3F000h`.
7. A declared shared-memory range lies completely inside available backing.
8. Upload order, image length, and CRC32 pass before the image becomes staged.

The manifest value `stack: 0000:0000` means that the reset handoff does not
initialize `SS:SP`. It does not allocate a stack automatically. A workload that
uses `PUSH`, `CALL`, `INT`, or interrupts must establish a valid stack before
those operations.

For a downward-growing stack, the workload author must leave sufficient space
below the initial `SP` and must keep the stack out of the shared-mailbox region.

## Shared mailbox: `3F000h-3FFFFh`

The top 4 KiB of Internal SRAM is reserved for ownership-transfer communication
between the Host and physical processor:

| Physical range | Size | Meaning |
|---|---:|---|
| `3F000h-3F01Fh` | 32 bytes | Canonical mailbox header |
| `3F020h-3FFFFh` | 4,064 bytes | Request or response payload |

### Header ABI

| Offset | Size | Field |
|---:|---:|---|
| `00h` | 4 | magic = `R86M` (`0x4D363852`, little-endian) |
| `04h` | 2 | ABI version |
| `06h` | 2 | header size, currently 32 |
| `08h` | 2 | owner |
| `0Ah` | 2 | status |
| `0Ch` | 4 | generation |
| `10h` | 2 | request length |
| `12h` | 2 | response length |
| `14h` | 4 | flags |
| `18h` | 8 | reserved |

Owner values are:

```text
0 = NONE
1 = HOST
2 = PROCESSOR
```

Status values are:

```text
0 = EMPTY
1 = REQUEST_READY
2 = PROCESSING
3 = RESULT_READY
4 = ERROR
```

`owner` is the publication/commit word. The current owner writes all other
header fields and payload bytes first, then performs one final 16-bit owner
write to transfer ownership. The receiver must not consume partially published
content.

General workload images may not overlap the mailbox, even if that workload does
not currently declare shared-memory use. This keeps the map stable and prevents
an image update from destroying an in-flight Host/processor exchange.

## RESET handoff: `FFFF0h-FFFFFh`

After RESET, an 8086-class processor begins fetching at physical `FFFF0h`. For a
general clock-stepped workload, RP2350 serves a generated 16-byte handoff there.

Without an explicit stack, the handoff contains:

```asm
jmp far entry_segment:entry_offset
; remaining bytes are NOP padding
```

With an explicit `SS:SP`, it contains the equivalent of:

```asm
cli
mov ax, stack_segment
mov ss, ax
mov sp, stack_offset
jmp far entry_segment:entry_offset
```

The largest form occupies 14 bytes and fits within the 16-byte reset window.
The handoff is generated from the already validated manifest. It is control
logic, not a general BIOS ROM and not persistent processor RAM.

## Prepared diagnostic ROM: `F0000h-FFFFFh`

The prepared acceptance/diagnostic runtime uses a separate memory personality.
RP2350 serves a 64 KiB native image in `F0000h-FFFFFh`, including its reset
vector, interrupt handlers, AAD-16 processor identity discriminator, and
diagnostic service code.

This mapping exists only when that prepared responder is active. It must not be
used as evidence that a general `.P86W` workload receives a BIOS, permanent ROM,
or heartbeat handler. General workload state comes from the workload manager,
execution clock, bus controller, and processor lifecycle—not from this optional
diagnostic image.

## Unmapped region: `40000h-EFFFFh`

No current canonical backing owns this 704 KiB range. A workload must not fetch,
read, write, or place a stack here. Access is not treated as valid memory and may
end in an explicit bus fault or timeout according to the active supervisor.

Leaving this range unmapped is deliberate:

- absent hardware is never represented as working RAM;
- PSRAM integration can be validated without silently changing existing maps;
- future cache/refill or banked schemes remain possible;
- a workload receives a clear failure instead of fabricated data.

## External PSRAM policy

The APS6404L expansion is not part of the validated map above. Installing and
detecting the chip is not sufficient to assign it a processor address range.
Before the map changes, the project must define and validate:

1. which 20-bit CPU ranges are backed by PSRAM;
2. whether the top reset/diagnostic region remains reserved;
3. byte-lane behavior and odd/even address access;
4. clock-stepped response deadlines;
5. Host/processor ownership and cache coherency, if caching is used;
6. fault behavior when PSRAM is absent or fails its probe;
7. workload-manifest compatibility and capability reporting.

Until that decision is committed, PSRAM remains an optional capacity backend
with no claimed CPU-visible address range.

## NOR flash and filesystems are not memory mapped

The RP2350 board has a 16 MiB W25Q128JV NOR flash, but it is not directly visible
on the 8086/V30 memory bus:

```text
NOR 000000h-3FFFFFh = RP2350 firmware reservation
NOR 400000h-FFFFFFh = 12 MiB FAT16 RP-FLASH volume
```

Those are NOR-device offsets, not processor physical addresses. `flash:/` files
are accessed through RP2350 services. They must be loaded into a processor
memory backing before the physical CPU can execute their contents.

The same rule applies to future `sd:` storage.

## Host memory-service rules

The Host shell uses physical processor addresses:

```text
mem read <physical-address> [length]
mem write <physical-address> <byte>...
mem load <Host-file> <physical-address>
mem save <physical-address> <length> <Host-file>
```

Examples:

```text
mem read 0x10000 32
mem read 0x3F000 32
mem write 0x20000 DE AD BE EF
```

Safety rules depend on workload state:

- when no workload is running, Host writes may target the available 256 KiB
  backing;
- while a workload is running, general Host writes are disabled;
- while running, writes are allowed only inside the mailbox and only while the
  mailbox is Host-owned;
- reads must still name a valid backed range, but concurrent reads of
  processor-mutated memory are observations and may not be an atomic snapshot.

Host memory transfer records carry at most 40 data bytes per 64-byte HID record.
This transport chunk size does not alter the processor memory map.

## Current guarantees and non-guarantees

### Guaranteed now

- a 20-bit, 1 MiB processor address model;
- Internal SRAM at `00000h-3FFFFh`;
- workload-image exclusion from `3F000h-3FFFFh`;
- a 4 KiB shared mailbox with a fixed 32-byte header;
- a generated 16-byte reset handoff at `FFFF0h`;
- bounded image upload with length, order, range, entry, and CRC32 validation;
- Host physical-address memory inspection;
- physically validated Intel 8086 execution from Internal SRAM.

### Not guaranteed yet

- PSRAM-backed processor execution;
- RAM at `40000h-EFFFFh`;
- a BIOS or PC-compatible map;
- VGA memory at `A0000h-BFFFFh`;
- option ROMs at `C0000h-EFFFFh`;
- a permanent ROM at `F0000h-FFFFFh` for general workloads;
- direct CPU memory mapping of NOR flash, `flash:`, or `sd:`.

## Source of truth

The executable definitions are maintained in:

- `firmware/memory/internal_sram_backing.h` — Internal-SRAM base and size;
- `firmware/memory/shared_mailbox.h` — mailbox base, size, and header ABI;
- `firmware/runtime/workload_manager.c` — manifest range validation;
- `firmware/runtime/canonical_runtime.c` — reset handoff and diagnostic ROM;
- `tools/rp86_runtime/workload.py` — Host-side manifest validation;
- `tools/rp86_runtime/memory.py` — Host memory-transfer records.

If prose and executable constants ever disagree, the mismatch is a defect. The
map must be updated as one contract rather than maintained as independent
claims in unrelated files.
