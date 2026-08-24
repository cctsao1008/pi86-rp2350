# Memory and Shared-Storage Architecture

## 1. Scope

This document defines how the Host, RP2350, and physical NEC V30 use volatile
memory and persistent storage.

The governing rule is:

> **Host and V30 share content, but they do not share low-level ownership.**

The RP2350 owns every physical controller and arbitrates access.

## 2. Canonical terms

| Term | Meaning |
|---|---|
| **RP2350 Internal SRAM** | on-chip memory used by firmware, realtime engines, mailbox, prepared windows, and short traces |
| **External PSRAM** | principal V30 execution-memory backing and Host/V30 shared volatile workspace |
| **External NOR Flash** | non-volatile device containing firmware/reserved space and the shared `flash:` FAT volume |
| **SD Card** | optional removable `sd:` FAT volume |
| **V30-visible memory** | the 20-bit physical address space presented to the V30 |
| **Shared memory** | an explicitly assigned PSRAM/Internal-SRAM region accessible to Host and V30 through RP2350 ownership |
| **Prepared window** | RP2350 state arranged in advance to meet a bounded V30 bus deadline |

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

Selected windows may be presented to the V30. Internal SRAM is not reserved only
for firmware, but it is not the primary bulk V30 execution memory.

### External PSRAM

External PSRAM is the principal volatile resource for:

- native V30 workload code;
- data, stack, and heap;
- Host/V30 shared-memory regions;
- large transfers and traces;
- restart snapshots when required.

The Host addresses assigned V30-visible memory through RP2350 operations. It does
not receive raw access to the PSRAM controller, allocator metadata, or
firmware-private regions.

General PSRAM-backed arbitrary V30 execution is still an implementation and
physical-validation gate. The architecture describes its role without claiming
the fixed-`READY` bus path is complete.

### External NOR Flash

NOR is divided conceptually:

```text
External NOR Flash
+-- RP2350 firmware / recovery / reserved platform area
`-- PI86FLASH shared FAT volume
    +-- workloads/
    +-- data/
    +-- config/
    +-- shared/
    `-- results/
```

The public name is `flash:`:

```text
flash:/workloads/hello.bin
flash:/data/input.dat
flash:/results/output.txt
```

The FAT volume sits on a Flash-aware block layer that handles erase geometry and
wear behavior. Raw media and internal numeric drive IDs are not public APIs.

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

A V30 service may initially be restricted to directories such as:

```text
flash:/shared/
flash:/results/
sd:/shared/
sd:/results/
```

## 7. Physical timing policy

The current Pi86 HAT keeps V30 `READY` asserted. An active bus cycle cannot wait
for:

- Host software or USB;
- FAT operations;
- NOR or SD access;
- arbitrary PSRAM latency;
- an unbounded M33 or inter-core lookup.

Timing-critical state must be prepared for PIO/DMA or served by another measured
bounded mechanism. Storage and Host activity happen outside the current V30
cycle.

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

1. detect and test External PSRAM;
2. provide Host PSRAM read/write through RP2350 ownership;
3. mount and exercise `flash:`;
4. mount/unmount and hot-remove `sd:`;
5. upload a flat V30 workload while stopped;
6. prove reset handoff and native execution;
7. validate general PSRAM-backed V30 execution;
8. add shared memory and V30 file services;
9. integrate fault preservation, timeout, and restart.

## 10. Related documents

- [`architecture.md`](architecture.md) — canonical role and runtime model
- [`host_runtime_architecture.md`](host_runtime_architecture.md) — detailed runtime contract
- [`host_protocol.md`](host_protocol.md) — Host operations
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) — fixed-`READY` constraint
- [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — current architecture decision
