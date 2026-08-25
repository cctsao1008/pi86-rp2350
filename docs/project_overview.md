# pi86-rp2350 Project Overview

## Identity

> **pi86-rp2350 is a host-managed bare-metal processor runtime for a real NEC V30.**

It is a **Host-Managed Bare-Metal Physical Processor Runtime**: a modern
remote-processor runtime for a vintage physical CPU.

The V30 executes native x86-class code. It is not emulated. The Host loads,
communicates with, observes, and restarts workloads. The RP2350 owns the
resources and physical bus connecting them.

## Responsibility split

```text
Host      = Runtime Controller
RP2350    = Companion Resource and Bus Controller
NEC V30   = Bare-Metal Remote Physical Processor
```

The Host supplies runtime services such as workload control, stdio, files,
status, timeout, and restart. The RP2350 arbitrates memory, storage, mailbox,
interrupt, clock, reset, PIO, DMA, and the physical V30 bus. The V30 owns native
instruction execution and architectural CPU state.

## Operating model

```text
load -> run -> communicate -> observe -> exit / fault / timeout -> restart
```

A workload is native V30 machine code plus launch metadata. The RP2350 loads
assigned V30-visible memory, prepares a small reset handoff at `FFFF0h`, and
releases the processor.

A traditional BIOS, boot sector, DOS, or V30 operating system is not required.
Those remain possible workloads, not the project foundation.

## Shared resources

The central ownership rule is:

> **Host and V30 share content, but they do not share low-level ownership.**

- **RP2350 Internal SRAM** holds firmware, realtime state, mailbox, prepared
  windows, and short traces.
- **External PSRAM** is designated as the principal V30 execution memory and
  shared volatile workspace; general PSRAM-backed V30 execution remains an
  unvalidated physical integration target.
- **External NOR Flash** contains firmware/reserved space and the shared
  `flash:` FAT volume.
- **SD Card** is the optional removable `sd:` FAT volume.

The RP2350 is the single owner of PSRAM, Flash, SD, FAT metadata, PIO, DMA,
clock, reset, and interrupt controllers. Host and V30 requests pass through
explicit RP2350 services.

## Host runtime

The reference Python client is a small remote shell. Its framework covers:

- `load`, `run`, `stop`, and `restart`;
- stdin/stdout and mailbox exchange;
- file operations on `flash:` and `sd:`;
- V30-visible memory transfer and inspection;
- `status`, `top`, trace, timeout, and fault evidence.

Python is not mandatory. Other clients may implement the same Host Protocol.
ChatGPT or Codex can communicate with the V30 as Host clients, but AI is not a
V30-visible architectural concept.

## Physical timing

The existing Pi86 HAT keeps V30 `READY` asserted. Current-cycle bus behavior
must therefore remain in PIO/DMA and prepared RP2350 state. Host latency, USB,
FAT, NOR, SD, and arbitrary PSRAM accesses do not answer a live V30 cycle.

General PSRAM-backed arbitrary native execution remains a physical integration
gate and is not yet claimed as accepted hardware behavior.

## What makes it different

A conventional PC clone recreates BIOS, chipset, storage, and operating-system
expectations around a processor. `pi86-rp2350` instead keeps the V30 bare metal
and moves the surrounding runtime to a modern Host and RP2350 companion.

The goal is not merely to boot an old CPU again. It is to give that physical CPU
a modern way to receive work, communicate, remain observable, fail honestly, and
restart.

## Read next

- [Canonical architecture](architecture.md)
- [Detailed Host runtime contract](host_runtime_architecture.md)
- [Host shell](host_runtime_shell.md)
- [Memory and storage](memory_architecture.md)
- [Host Protocol](host_protocol.md)
- [Hardware contract](hardware_contract.md)
- [Physical validation](validation/)
