# pi86-rp2350 Project Overview

## Identity

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**

It is a **Host-Managed Bare-Metal Physical Processor Runtime**: a modern
remote-processor runtime for a vintage physical CPU.

The installed Intel 8086 or NEC V30 executes native x86-class code. It is not emulated. The Host loads,
communicates with, observes, and restarts workloads. The RP2350 owns the
resources and physical bus connecting them.

## Responsibility split

```text
Host      = Runtime Controller
RP2350    = Companion Resource and Bus Controller
8086/V30  = Bare-Metal Remote Physical Processor
```

The Host supplies runtime services such as workload control, stdio, files,
status, timeout, and restart. The RP2350 arbitrates memory, storage, mailbox,
interrupt, clock, reset, PIO, DMA, and the physical processor bus. The installed processor owns native
instruction execution and architectural CPU state.

## Operating model

```text
load -> run -> communicate -> observe -> exit / fault / timeout -> restart
```

A workload is native 8086-class machine code plus launch metadata. The RP2350 loads
assigned V30-visible memory, prepares a small reset handoff at `FFFF0h`, and
releases the processor.

The canonical flat image is `hello.bin`. The workload itself distinguishes
Intel 8086 behavior from NEC V30 behavior with `AAD 16`, then reports `HELLO
INTEL 8086` or `HELLO NEC V30`. It is both the first loader example and a
physical execution witness; its general Internal-SRAM-backed arbitrary execution
path remains to be integrated and validated on hardware.

A traditional BIOS, boot sector, DOS, or V30 operating system is not required.
Those remain possible workloads, not the project foundation.

## Shared resources

The central ownership rule is:

> **Host and V30 share content, but they do not share low-level ownership.**

- **RP2350 Internal SRAM** holds firmware and realtime state and is also the
  first native workload-execution, processor-visible RAM, and shared-memory
  tier.
- **External PSRAM** is an optional capacity tier for larger workloads, bulk
  shared memory, snapshots, and cache/refill backing; processor execution from
  that tier remains a separate physical integration target.
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

General arbitrary-address Internal-SRAM-backed native execution is the next
physical integration gate. PSRAM capacity follows only after a measured
staging/cache policy exists.

## What makes it different

A conventional PC clone recreates BIOS, chipset, storage, and operating-system
expectations around a processor. `pi86-rp2350` instead keeps the Intel 8086 or NEC V30 bare metal
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
