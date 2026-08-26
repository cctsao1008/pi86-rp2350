# Host Runtime Contract

**Status:** Detailed contract subordinate to [`architecture.md`](architecture.md)
**Scope:** Host runtime, RP2350 resource ownership, and physical Intel 8086 / NEC V30 execution

## 1. Purpose

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**

The architecture class is **Host-Managed Bare-Metal Physical Processor
Runtime**: a modern remote-processor runtime for a vintage physical CPU.

It does not emulate the V30 and does not rebuild a traditional PC around it.
The V30 executes native bare-metal workloads. The Host supplies the surrounding
runtime, while the RP2350 owns the resources and physical bus that connect them.

> **The operating system stays on the Host. The physical 8086 or V30 runs assigned work.**

The operating model is deliberately small:

```text
Load -> Run -> Communicate -> Observe
                              |
                       Exit / Fault / Timeout
                              |
                           Restart
```

## 2. Architectural boundary

```text
Host OS
+-- Python Host shell
+-- optional C/Rust/Web clients
`-- optional Codex/ChatGPT client
             |
             | control, files, memory, stdio, status
             v
RP2350
+-- Host protocol
+-- resource ownership and permission enforcement
+-- workload loader and supervisor
+-- Internal SRAM realtime state
+-- External PSRAM manager
+-- External NOR FAT filesystem owner
+-- SD FAT filesystem owner
+-- clock, reset, interrupt, trace, and restart control
`-- PIO/DMA physical V30 bus engine
             |
             v
Physical Intel 8086 / NEC V30
`-- native bare-metal workload
```

The fixed responsibility split is:

```text
Host   = Runtime Controller
RP2350 = Companion Resource and Bus Controller
V30    = Bare-Metal Remote Physical Processor
```

Codex and ChatGPT may use the Host protocol, but neither is required by the
machine. To the V30 they are simply possible sources of input and consumers of
output.

## 3. What this project is not

The core architecture is not:

- a V30 emulator or instruction interpreter;
- a PC/XT clone;
- BIOS-first or DOS-first;
- a complete operating system running on the V30;
- a recreation of fixed 825x-era peripherals;
- a multi-process, multi-user, or virtual-memory environment;
- a system in which the Host answers individual V30 bus cycles.

BIOS, DOS, ELKS, and compatibility services may be loaded as experiments or
workloads. They do not define the platform and must not become prerequisites for
native workload execution.

## 4. Resource model

### 4.1 RP2350 Internal SRAM

Internal SRAM is primarily the RP2350's immediate working memory:

- PIO and DMA state;
- mailbox and interrupt data;
- Host protocol buffers;
- cache and prepared response windows;
- trace buffers;
- firmware runtime state.

Selected Internal SRAM windows may be presented to the V30 when useful. It is
not reserved exclusively for firmware, but it is not the V30's primary bulk
memory.

### 4.2 External PSRAM

External PSRAM is intended to become the principal V30 execution-memory backing
and shared volatile workspace. Its assigned content will include:

- native workload code;
- data, stack, and heap;
- Host/V30 shared-memory regions;
- large transfer and trace buffers;
- snapshots and restart state when required.

The Host addresses the V30-visible address space through the RP2350. It does not
receive raw ownership of the PSRAM controller or RP2350-private metadata.

### 4.3 External NOR Flash

External NOR Flash has two conceptual areas:

```text
W25Q128JV (16 MiB)
+-- 0x000000-0x3FFFFF  RP2350 firmware / reserved (4 MiB)
`-- 0x400000-0xFFFFFF  RP-FLASH FAT16 volume (12 MiB)
```

The public volume name is:

```text
flash:
```

Example paths:

```text
flash:/hello.bin
flash:/input.dat
flash:/output.txt
```

The shared NOR volume uses FatFs R0.16p2 through a 4 KiB-aligned Flash-aware
block layer. First-boot format, FAT16 persistence, volume-label migration, and
media self-test are physically validated. The FAT implementation's internal
numeric drive identifier is private and is not exposed to Host or processor
software.

### 4.4 SD Card

The SD card is a removable FAT volume for larger or portable content:

- workload libraries;
- datasets;
- trace export;
- snapshots;
- import, backup, and offline exchange.

The public volume name is:

```text
sd:
```

Example paths:

```text
sd:/workloads/demo.bin
sd:/datasets/input.dat
sd:/traces/run001.log
```

FAT32 is the baseline SD format. SD absence or removal must not prevent
`flash:`, PSRAM execution, console, monitoring, or restart from operating.

## 5. Shared-resource rule

> **Host and V30 share content, but they do not share low-level ownership.**

The RP2350 is the sole owner of:

- PSRAM allocation and physical access;
- External NOR and SD controllers;
- FAT metadata and synchronization;
- clock, reset, interrupt, PIO, and DMA state;
- bus-engine and firmware-private memory.

Host and V30 requests are serialized and checked by the RP2350. Neither client
directly mounts a raw device or changes filesystem metadata, PSRAM allocation,
or bus-engine state.

## 6. Roles and permissions

The permission model has three principals only. It does not reproduce Unix
users, groups, or general ACLs.

| Resource | RP2350 | Host | V30 workload |
|---|---|---|---|
| Firmware/reserved Flash | owner R/W | controlled firmware update | no access |
| Bus engine and metadata | owner R/W | status only | no access |
| V30 code memory | owner R/W | R/W while stopped | R/X while running |
| V30 data/stack/heap | owner R/W | R/W while stopped; observe while running | R/W |
| Shared memory | owner/arbitrator | R/W through protocol | R/W through assigned region |
| `flash:` FAT volume | filesystem owner | R/W through protocol | service-mediated access |
| `sd:` FAT volume | filesystem owner | R/W through protocol | service-mediated access |
| Mailbox and stdio | owner/arbitrator | R/W through protocol | R/W through service ABI |
| Trace | owner/producer | read/save/control | no direct access |
| Clock/reset/interrupt | owner/control | commands through protocol | receives execution effects |

The public baseline is the volume root (`flash:/`). The Host can list the
volume, inspect capacity, read files, and atomically upload/replace files through
the mediated 64-byte HID service. Processor file services and the remaining
Host mutation commands are separate later integrations.

## 7. Runtime states

The RP2350 maintains one explicit runtime state:

```text
EMPTY -> LOADED -> RUNNING -> EXITED
                    |  |
                    |  `-> FAULT / TIMEOUT
                    |              |
                    `--------------+-> STOPPED -> RESTART or LOAD
```

### EMPTY

No workload is selected. Files, storage, status, and loading remain available.

### LOADED / STOPPED

The V30 is not executing the workload. The Host may load or modify V30-visible
memory, inspect state, manage files, and configure the launch context.

### RUNNING

The V30 owns its code, data, stack, and heap. The Host normally observes rather
than changing ordinary workload memory. Mailbox, stdio, approved shared memory,
file services, trace, stop, and restart remain available.

### EXITED

The workload reported normal completion. Results, memory, and trace remain
available to the Host.

### FAULT / TIMEOUT

A workload may crash, loop forever, produce invalid activity, or stop replying.
This is a valid workload outcome, not a platform design failure. The Host shell
remains alive, preserves available evidence, reports the failure, and waits for
`restart`, `stop`, or a new `load` command.

## 8. Workload format and launch

Development tools may retain ELF files for symbols and debugging. The physical
transfer format is initially a flat binary plus launch metadata:

```text
image          native V30 flat binary
load_address   V30 physical address
entry          initial CS:IP
stack          initial SS:SP
segments       initial DS and ES when specified
```

Example Host operation:

```text
load hello.bin --address 0x10000 --entry 1000:0000
run
```

The RP2350 loads the image into V30-visible memory, prepares a minimal Reset
Handoff at `FFFF0h`, establishes the launch context, releases RESET, and observes
execution. A traditional BIOS is not required.

ROM images remain possible for special fixed tests. DOS COM/MZ loaders are not
part of the initial native-workload contract.

## 9. V30 runtime services

The V30 receives a small service ABI rather than an operating system. The
minimum useful services are:

- stdin/read;
- stdout/write;
- file open/read/write/seek/close;
- workload exit/result;
- heartbeat or status response;
- optional shared-memory notification.

Services are exposed through RP2350-managed mailbox and interrupt mechanisms.
The V30 never directly owns USB, FAT, NOR Flash, SD, or PSRAM controllers.

## 10. Host Runtime Shell

The Python Host runtime presents one small remote shell. The complete command
framework is defined from the beginning even when a backend is not yet
implemented. An unavailable backend must report `NOT AVAILABLE`; it must never
claim a hardware operation succeeded.

| Area | Commands |
|---|---|
| Workload | `load`, `run`, `stop`, `restart` |
| Console | `send`, `console`, `stdin`, `stdout` |
| Files | `ls`, `cat`, `put`, `get`, `rm`, `mv` |
| Storage | `df`, `mount`, `unmount`, `sync` |
| Memory | `mem read`, `mem write`, `mem load`, `mem save` |
| Observation | `status`, `top`, `info`, `trace`, `regs` |
| Supervision | `ping`, `timeout`, heartbeat, restart |
| Shell | `help`, `quiet`, `verbose`, `quit` |

Example session:

```text
pi86> put hello.bin flash:/workloads/hello.bin
pi86> load flash:/workloads/hello.bin --address 0x10000 --entry 1000:0000
pi86> run
HELLO FROM NEC V30

pi86> top
V30       ALIVE @ 1.000 MHz
Workload  hello.bin
Runtime   00:00:08
Heartbeat 8 completed / 0 lost
PSRAM     48 KiB used
flash:    MOUNTED
sd:       NOT PRESENT

pi86> mem read 0x00100 16
00100: 34 12 78 56 00 00 00 00 00 00 00 00 00 00 00 00

pi86> restart
V30 reset; workload restarted
```

`top` describes the single physical V30 environment, not an operating-system
process list. It reports liveness, clock, workload, runtime, heartbeat latency
and loss, memory use, volume state, I/O counters, interrupt activity, bus errors,
watchdog state, and restart count.

Register reporting is not assumed to be a hardware debug port. `regs` is valid
only when a workload monitor or interrupt service has published a register
snapshot.

## 11. Host protocol

The Host protocol carries typed operations rather than shell text as its wire
contract. The shell, Python API, and future C/Rust/Web clients translate their
operations into the same protocol.

The protocol must support:

- capability negotiation;
- sequence-bound requests and replies;
- workload metadata and chunked transfer;
- V30-visible memory read/write;
- filesystem and storage operations;
- stdio and mailbox records;
- status, trace, timeout, stop, and restart;
- explicit error and unavailable responses.

Large payloads may use a bulk transport while control and status retain the same
logical operations. USB or Host latency must not become a dependency of an
active V30 bus cycle.

## 12. Physical execution boundary

The physical V30 owns instruction execution, register state, and control flow.
PIO/DMA and prepared RP2350 state satisfy timing-critical bus behavior. Host
software, USB, FAT operations, NOR access, SD access, and arbitrary PSRAM
transactions do not answer a current V30 bus cycle directly.

General PSRAM-backed arbitrary execution remains a physical implementation gate.
The existing Pi86 HAT holds `READY` asserted, so the implementation must validate
a bounded prepared or staged hit path on that hardware. The Host architecture
and shell do not pretend that this gate has already passed.

## 13. Failure and recovery policy

The platform guarantees ownership and electrical safety, not workload success.

When a workload stops responding:

1. the Host reports `NOT RESPONDING` or `TIMEOUT`;
2. available memory, status, and trace are retained;
3. Host stdio and control remain alive;
4. the user may inspect or save evidence;
5. `restart` resets and launches the workload again.

Automatic restart may be added as an option. Manual restart is the baseline so
failure evidence is not destroyed without user intent.

## 14. Implementation sequence

The architecture is fixed; implementation proceeds by connecting backends to
the stable Host shell:

1. Host shell framework and capability reporting;
2. External PSRAM detection, integrity, and Host read/write;
3. External NOR shared FAT volume as `flash:`;
4. SD FAT volume as `sd:` with insert/remove handling;
5. workload upload and stopped-state memory loading;
6. minimal Reset Handoff and Internal-SRAM execution proof;
7. PSRAM-backed arbitrary native V30 execution;
8. stdio and mailbox runtime;
9. V30 file services;
10. `top`, trace, timeout, fault preservation, and restart integration.

This sequence does not introduce new architecture. Each step only implements an
already-defined service.

## 15. Definition of the project

The complete design can be summarized in one sentence:

> **Host controls the runtime; RP2350 owns shared resources and the physical
> bus; the real Intel 8086 or NEC V30 executes bare-metal native workloads.**

Or, operationally:

> **Load. Run. Talk. Watch. Restart.**
