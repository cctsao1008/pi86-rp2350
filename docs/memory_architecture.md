# Memory and Shared-Storage Architecture

## 1. Scope

This document defines how the Host, RP2350, and physical NEC V30 use volatile
memory and persistent storage.

The governing rule is:

> **Host and V30 share content, but they do not share low-level ownership.**

The RP2350 owns every physical controller and arbitrates access.

## 2. Canonical terms

| Term | Architectural meaning | Current implementation / validation status |
|---|---|---|
| **RP2350 Internal SRAM** | on-chip memory for firmware/realtime state and the first workload-execution, CPU-visible RAM, and shared-memory tier | 256 KiB processor range (`00000h-3FFFFh`) is reserved; Host HID staging and bounded 1 MHz execution are validated; PACED general branch/loop/stack/RAM/I/O execution is physically validated |
| **External PSRAM** | optional capacity tier for larger workloads, bulk shared memory, snapshots, and cache/refill backing | SDK-backed detection/access framework implemented; direct/general processor execution is not physically validated |
| **External NOR Flash** | first 4 MiB reserved for firmware; final 12 MiB is the shared `flash:` FAT volume | FAT16 `RP-FLASH` mount, persistence, and media self-test physically validated; Host `ls`, `df`, `cat`, and atomic `put` are implemented; processor file services and remaining mutations remain open |
| **SD Card** | intended optional removable `sd:` FAT volume | GPIO safe-state initialization implemented; card/FAT service not implemented |
| **Processor-visible memory** | the 20-bit physical address space presented to the installed Intel 8086 or NEC V30 | Internal-SRAM upload/backing contract, bounded CONTINUOUS execution, and standalone general PACED execution are validated |
| **Shared memory** | an explicitly assigned PSRAM/Internal-SRAM region accessible to Host and V30 through RP2350 ownership | protocol and ownership model defined; general service not implemented |
| **Prepared window** | RP2350 state arranged in advance to meet a bounded V30 bus deadline | retained PIO/DMA implementations physically validated |

Do not equate a V30 address with one physical device. The RP2350 maps and
materializes the assigned memory.

## 3. Physical resources

### RP2350 Internal SRAM

Internal SRAM is immediate runtime memory for:

- firmware and per-core stacks;
- PIO/DMA descriptors, queues, and response state;
- mailbox, interrupt, and Host Protocol buffers;
- prepared or cached V30 windows;
- short trace and fault-preservation buffers.

Internal SRAM is also the first execution-memory backend for native workloads,
processor-visible writable RAM, and Host/processor shared windows. Canonical
firmware now reserves a 256 KiB backing pool mapped to processor addresses
`00000h-3FFFFh`. Host HID begin/data/commit transactions stage a flat image in
that pool with range, ordered-chunk, and CRC32 validation. The measured linker
map leaves about 224 KiB of main SRAM for firmware, realtime state, and future
integration growth.

The CONTINUOUS physical launch is accepted: a 16-byte Host-uploaded calculator
at `10000h` is fetched and executed by the real processor under `run`, `stop`,
and `restart` control. Separately, the PACED bus engine now serves general
Internal-SRAM code, data, and stack one complete clock pulse at a time. Its first
physical workload validated reset handoff, taken branches, `LOOP`, `PUSH`/`POP`,
byte/word RAM, and I/O publication across 218 serviced cycles.

The next integration gate places that PACED engine behind the canonical Host
lifecycle and adds cooperative CONTINUOUS/PACED switching, shared-memory behavior,
stdio, and timeout/restart handling without making External PSRAM a prerequisite.

### External PSRAM

External PSRAM is an optional capacity tier for:

- larger native workload code/data/stack/heap images;
- bulk Host/processor shared-memory regions;
- large transfers and traces;
- cache/refill backing outside the current processor bus cycle;
- restart snapshots when required.

The Host addresses assigned V30-visible memory through RP2350 operations. It does
not receive raw access to the PSRAM controller, allocator metadata, or
firmware-private regions.

PSRAM absence must not prevent small Internal-SRAM-backed workloads from being
loaded and executed. Any PSRAM-backed processor execution remains a separate
implementation and physical-validation gate.

### External NOR Flash

NOR is divided conceptually:

```text
W25Q128JV (16 MiB)
+-- 0x000000-0x3FFFFF  RP2350 firmware / reserved (4 MiB)
`-- 0x400000-0xFFFFFF  RP-FLASH FAT16 volume (12 MiB)
```

The public name is `flash:`:

```text
flash:/hello.bin
flash:/input.dat
flash:/output.txt
```

The volume uses FatFs R0.16p2, 512-byte logical sectors, 2 KiB allocation units,
and a 4 KiB read-modify-erase-program cache aligned to the NOR erase geometry.
Raw media and internal numeric drive IDs are not public APIs.

### SD Card

SD is the optional removable FAT32 volume `sd:`:

```text
sd:/workloads/demo.bin
sd:/datasets/input.dat
sd:/traces/run001.log
```

It is intended for larger workload libraries, datasets, trace export, snapshots,
backup, and offline exchange. Absence or removal of SD must not disable
`flash:`, PSRAM execution, Host control, monitoring, or restart.

## 4. Ownership and permissions

The RP2350 is the sole owner of:

- PSRAM allocation and physical access;
- NOR and SD controllers;
- FAT metadata and synchronization;
- PIO/DMA and bus-engine state;
- firmware-private Internal SRAM.

| Resource | Host | V30 workload |
|---|---|---|
| Firmware/reserved NOR | controlled update only | no access |
| V30 code memory | R/W while stopped | R/X while running |
| V30 data/stack/heap | R/W while stopped; observe by policy while running | R/W |
| Shared memory | R/W through Host Protocol | R/W within assigned region |
| `flash:` and `sd:` | file operations through Host Protocol | runtime file service |
| Mailbox/stdio | Host Protocol client | runtime service ABI |
| PIO/DMA/controller metadata | status only | no access |

The permission model is not a Unix ACL system. It only protects runtime
ownership and prevents Host or V30 code from corrupting RP2350-private state.

## 5. V30-visible memory

The NEC V30 exposes a 20-bit physical address space:

```text
00000h ------------------------- FFFFFh
             1 MiB
```

The runtime does not impose the IBM PC conventional-memory/VGA/BIOS layout.

A workload launch needs:

- a deterministic reset handoff at `FFFF0h`;
- an executable region;
- writable data/stack/heap;
- defined unmapped behavior;
- optional shared-memory or service regions.

Exact addresses belong to workload launch metadata and the RP2350 loader, not a
fixed PC layout.

## 6. Host/V30 sharing

### Shared volatile memory

The RP2350 allocates a region, returns a stable V30 address and Host handle, and
defines publication/ownership boundaries. The Host and V30 may exchange data
without receiving the PSRAM controller itself.

While the V30 is running, ordinary code/data memory belongs to the workload.
Host writes are limited to approved shared regions or explicit stopped-state
operations.

### Shared files

The RP2350 owns one FAT implementation per mounted volume. Host and V30 are file
service clients.

```text
Host FS request ----+
                    v
              RP2350 FAT owner -> NOR / SD
                    ^
V30 FS request -----+
```

Requests are serialized. “Shared” means one namespace and shared file content,
not simultaneous raw block-device ownership.

The initial public contract is the volume root (`flash:/`). Directory policy is
not embedded in the block layer and can be added later by the runtime service.

## 7. Physical timing policy

The current Pi86 HAT keeps processor `READY` asserted. The runtime uses two clock
policies instead of READY-generated wait states:

- CONTINUOUS uses a measured running clock plus PIO/DMA and prepared response state.
- PACED issues complete pulses and may pause with `CLK` low between pulses while
  M33 services the next memory or I/O operation.

The PACED policy has been physically validated with general Internal SRAM. It does
not stop midway through a pulse and does not let Host software or a filesystem
callback take ownership of an active electrical phase. Host, FAT, NOR, SD, and
unbounded PSRAM work remain outside the partially completed bus cycle.

This is a physical implementation constraint, not a requirement to reject
complex workloads. A workload may crash or time out; the Host reports the
outcome and can restart it.

## 8. Public storage contract

Stable volume names are:

| Volume | Media | Baseline format | Required |
|---|---|---|---|
| `flash:` | External NOR | FAT through Flash-aware block layer | yes |
| `sd:` | SD Card | FAT32 | no |

Public APIs use paths and file operations. They do not expose FatFs numeric drive
IDs, raw erase blocks, or SD sectors.

## 9. Implementation gates

The order of implementation does not change the architecture:

1. integrate the validated PACED engine into the canonical Host lifecycle;
2. add cooperative CONTINUOUS/PACED engine switching at an explicit safe point;
3. add stdio, fault reporting, timeout, and Host restart around general execution;
4. expose Internal-SRAM shared memory through RP2350 ownership;
5. complete remaining `flash:` file operations and processor file services;
6. detect/test External PSRAM and provide Host read/write;
7. add PSRAM-backed capacity through a measured staging/cache policy;
8. mount/unmount and hot-remove `sd:` and complete fault preservation.

## 10. Related documents

- [`architecture.md`](architecture.md) — canonical role and runtime model
- [`host_runtime_architecture.md`](host_runtime_architecture.md) — detailed runtime contract
- [`host_protocol.md`](host_protocol.md) — Host operations
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) — fixed-`READY` constraint
- [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — current architecture decision
