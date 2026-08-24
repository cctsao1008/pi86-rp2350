# pi86-rp2350 Project Overview

## Project identity

`pi86-rp2350` is a programmable machine platform built around a **physical NEC V30**, the original Pi86 V20/V30 HAT, and an RP2350.

The V30 executes native x86-class code and owns architectural CPU state. The RP2350 constructs, controls, and observes the surrounding execution environment through deterministic PIO/DMA bus logic, firmware-managed memory/storage, and a small host interface.

This is not an x86 emulator and not merely a board-for-board Pi86 port.

## Core model

The project has three actors:

```text
Host
  |
  | USB HID + CDC
  v
RP2350 Machine Platform
  |
  | deterministic physical bus
  v
Original Pi86 HAT
  |
  v
Physical NEC V30
```

The execution model is:

```text
Host requests / stored configuration
              |
              v
RP2350 prepares machine state
              |
              v
RP2350 releases V30 RESET
              |
              v
Physical V30 executes native workload
              |
              v
RP2350 retains observations/results
```

The governing principle is:

> **The RP2350 constructs the V30-visible machine before execution; the physical V30 then executes inside that prepared environment.**

The hard timing boundary is:

> **PIO/DMA and bounded on-chip state own current-cycle V30 timing. Arm software, USB, storage, and host tools operate outside that active-cycle path.**

## RP2350 firmware scope

The minimum complete firmware is intentionally small and has six responsibilities:

- V30 Bus Engine;
- Machine Control;
- Memory;
- Workload / Reset Handoff;
- Host Interface;
- Persistent Storage.

These are responsibilities rather than mandatory software layers.

BIOS APIs, DOS, ELKS, PIC/PIT/PPI compatibility, PC memory maps, V30 filesystem services, persistent heartbeat runtimes, and AI-specific services are optional capabilities or workloads.

## Host interface

The canonical host interface is language-independent:

```text
USB HID = structured command / response
USB CDC = log / diagnostic / observation
```

The project defines the wire protocol and may provide basic/sample host programs. Python, C, Rust, PowerShell, Web applications, CLIs, scripts, and AI agents are all possible clients; none is a required SDK layer.

Host operations cover only machine needs such as reset/clock control, state query, memory access, persistent storage, workload preparation/execution, and trace control.

See [`host_protocol.md`](host_protocol.md).

## Reset and workload execution

The V30 architectural reset fetch at physical `FFFF0h` is treated as a **Reset Handoff Point**, not as an implicit BIOS entry.

A minimal workload consists of native V30 code plus small launch metadata. The initial launch contract includes:

```text
entry CS:IP
initial SS:SP
initial DS
initial ES
```

The RP2350 prepares the required memory and a deterministic handoff sequence before releasing RESET.

A BIOS -> boot sector -> loader -> operating-system chain is therefore optional rather than fundamental.

## Memory model

Physical resources and V30 address semantics are deliberately separated.

Canonical physical resources are:

- **RP2350 Internal SRAM**;
- **External NOR Flash**;
- **External PSRAM**;
- **SD Card** when present.

The **V30 Memory Map** describes CPU-visible semantics. The **Backing Resource** describes how a mapped region is materialized.

The minimum useful V30 Memory Map requires:

- a Reset Handoff Region;
- an Executable Region;
- a Writable RAM Region;
- defined behavior for unmapped addresses.

No IBM PC conventional-memory, VGA, BIOS-ROM, IVT, or device region is required by the core architecture.

See [`memory_architecture.md`](memory_architecture.md).

## Memory and storage hierarchy

### RP2350 Internal SRAM

Used for firmware runtime, deterministic bus state, PIO/DMA queues and descriptors, explicitly prepared V30 working windows, and short trace/fault buffers.

It is not intended to be the main bulk V30 RAM resource.

### External NOR Flash

The current RP2350-PiZero provides 16 MB of external NOR Flash. It holds RP2350 firmware, reserved recovery capacity, persistent metadata/configuration, and filesystem-backed machine assets.

### External PSRAM

External PSRAM is part of the **target machine configuration** as bulk volatile backing/workspace. It separates large V30 working state, workload staging, snapshots, and long traces from deterministic Internal SRAM.

An Internal-SRAM + NOR-Flash configuration remains supported for bring-up and diagnostics, but it is not the target machine memory configuration.

External PSRAM is not automatically a valid current-cycle responder; current-cycle data must use a separately validated bounded mechanism, initially explicit preparation into on-chip deterministic state.

### SD Card

SD Card support is optional removable bulk storage. It may be useful for large datasets, traces, snapshots, images, or offline exchange, but it is not part of the core machine.

## Persistent filesystem

External NOR Flash may contain a LittleFS machine-asset filesystem.

Ownership follows one rule:

> **One filesystem, one owner, multiple clients.**

The RP2350 is the sole filesystem owner. The Host accesses files through HID operations. V30 file access is optional and, if added, is mediated by an RP2350 service rather than direct Flash/filesystem ownership.

## Deterministic execution boundary

The original Pi86 HAT keeps V30 `READY` asserted. Therefore an active V30 transaction cannot wait for an arbitrary M33 lookup, inter-core round trip, USB transaction, filesystem operation, External NOR Flash access, or unbounded External PSRAM access.

Current-cycle response state must already be represented by a bounded deterministic mechanism such as prepared RP2350 Internal SRAM feeding PIO/DMA.

The first implementation uses explicit prepared windows rather than a general cache hierarchy.

## Optional workloads and compatibility

PC-class behavior remains useful as a compatibility and stress-test profile, not as the project definition.

Optional examples include:

- diagnostic ROMs and assembly tests;
- BIOS code;
- 8259A/PIC, PIT, or PPI compatibility;
- DOS or ELKS;
- PC memory maps;
- disk-image boot paths;
- display and keyboard services;
- host/V30 mailbox or persistent-runtime services.

The repository preserves these mechanisms and validation results because they remain useful engineering evidence.

## Host and AI

AI is simply one possible Host client. It receives no special realtime role and does not participate in V30 bus timing.

Existing Host Bridge / Companion Service / AI experiments demonstrate that HID/CDC and V30 mailbox interaction can work on physical hardware. Their validated record framing and mechanisms may be reused, but BIOS `INT 60h`, PIT heartbeat, or AI-specific behavior is not required by the core machine model.

## Hardware baseline

The current physical platform consists of:

- Waveshare RP2350-PiZero;
- Raspberry Pi RP2350B;
- physical NEC V30 `D70116C-8` / `uPD70116C-8`;
- original Homebrew8088 Pi86 V20/V30 HAT;
- Raspberry Pi-compatible 40-pin mechanical interface;
- 16 MB External NOR Flash;
- External PSRAM in the target machine configuration;
- native USB HID/CDC host interface.

The existing Pi86 HAT remains the working hardware baseline unless a demonstrated architectural limitation requires reconsideration.

## Project lineage

`pi86-rp2350` builds directly on the Homebrew8088 Pi86 project and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor and service memory and I/O transactions in software.

`pi86-rp2350` preserves that physical CPU/HAT concept while moving the timing-critical boundary into RP2350 PIO, DMA, and deterministic on-chip state, then exposing a small machine-control and observation interface to modern hosts.

## Document map

- [`../README.md`](../README.md) - public project introduction
- [`README.md`](README.md) - documentation index
- [`architecture.md`](architecture.md) - canonical machine architecture
- [`memory_architecture.md`](memory_architecture.md) - memory resources, V30 Memory Map, backing policy
- [`host_protocol.md`](host_protocol.md) - language-independent HID/CDC Host Protocol
- [`dual_core_partitioning.md`](dual_core_partitioning.md) - realtime/service ownership model
- [`hardware_contract.md`](hardware_contract.md) - canonical physical interface
- [`companion_service_abi.md`](companion_service_abi.md) - validated optional/historical Companion Service record and mailbox mechanism
- [`validation/`](validation/) - physical validation records
- [`adr/`](adr/) - architecture decisions and their consequences
