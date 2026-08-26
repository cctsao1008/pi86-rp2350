# ADR 0008: Adopt the Host-Managed Bare-Metal Processor Runtime

**Status:** Accepted
**Date:** 2026-08-24

## Context

The project began by bringing up a physical NEC V30 on the original Pi86 HAT.
It then validated PIO/DMA bus response, native ROM/RAM behavior, interrupts,
Host mailbox exchange, USB HID/CDC communication, and a persistent heartbeat.

During that work the project was described at different times as a Pi86 port,
companion chip, software-defined chipset, programmable machine, Native BIOS
platform, PC-compatible machine, and Host-constructed V30 machine.

Those descriptions helped individual development stages, but they left the
project tied to PC-era assumptions or made the RP2350 appear to define the
purpose of the processor. The final direction is simpler: a modern Host sends
work to a real vintage processor and the RP2350 supplies the resources and
physical interface required to execute it.

## Decision

The canonical project definition is:

> **pi86-rp2350 is a host-managed bare-metal processor runtime for a real NEC V30.**

The architecture class is:

> **Host-Managed Bare-Metal Physical Processor Runtime**
> *A modern remote-processor runtime for a vintage physical CPU.*

The three roles are fixed:

```text
Host      = Runtime Controller
RP2350    = Companion Resource and Bus Controller
NEC V30   = Bare-Metal Remote Physical Processor
```

The operating model is:

```text
load -> run -> communicate -> observe -> exit / fault / timeout -> restart
```

### Host

The Host owns workload selection, launch intent, stdio, file operations,
observation, timeout, and restart. Python is the first reference client, not a
mandatory architecture layer.

### RP2350

The RP2350 owns the V30 bus and all low-level shared resources. It arbitrates
PSRAM, NOR, SD, FAT, mailbox, interrupt, clock, reset, PIO, and DMA. Host and
V30 share content through RP2350 services rather than sharing controllers.

### NEC V30

The physical V30 executes native bare-metal code and owns architectural CPU
state. It may complete, fault, hang, or time out. The Host observes the outcome
and may restart it.

## Resource decision

- RP2350 Internal SRAM is runtime/realtime working memory and the first native
  workload-execution, processor-visible RAM, and shared-memory tier.
- External PSRAM is an optional capacity tier for larger workloads, bulk shared
  memory, snapshots, and cache/refill backing. It is not a prerequisite for
  native workload execution, and PSRAM-backed execution requires separate
  physical validation.
- External NOR contains platform-reserved space plus a shared FAT volume named
  `flash:`.
- SD is the optional removable FAT volume named `sd:`.
- The RP2350 is the sole filesystem and physical-controller owner.

## Compatibility decision

BIOS, DOS, ELKS, PC memory maps, and 825x-compatible peripherals are optional
workloads or experiments. They are not the primary roadmap, do not define the
runtime, and are not required for native V30 execution.

AI is also optional. ChatGPT/Codex and other agents are Host clients using the
same protocol as conventional software.

## Physical constraint

The current Pi86 HAT keeps V30 `READY` asserted. PIO/DMA and prepared RP2350
state remain responsible for timing-critical cycles. Host software, USB,
filesystems, and arbitrary storage latency do not answer current cycles.

General arbitrary-address Internal-SRAM-backed execution remains an
implementation gate. PSRAM follows as a capacity tier after a bounded physical
staging/cache mechanism is validated.

## Consequences

- README and canonical documentation use the runtime terminology.
- The Host shell is the primary user-facing model.
- Current issues describe runtime, PSRAM, storage, workload, stdio, monitoring,
  and restart work.
- Former BIOS-first, PC-machine, machine-profile, and replacement-board plans
  are archived rather than treated as active architecture.
- Validation and story documents retain their original evidence and wording.
- A workload crash is reported and recoverable; it is not rejected merely
  because success cannot be guaranteed.

## Supersedes

This ADR supersedes the active architectural role of:

- ADR 0002, the V30 companion-chip/PC bring-up roadmap;
- ADR 0005, Host Bridge/Companion Service as the project-level terminology;
- ADR 0007, the Host-Constructed V30 Machine Model.

Their historical context is preserved under `docs/archive/`.

## Related documents

- [`../architecture.md`](../architecture.md)
- [`../host_runtime_architecture.md`](../host_runtime_architecture.md)
- [`../host_runtime_shell.md`](../host_runtime_shell.md)
- [`../memory_architecture.md`](../memory_architecture.md)
- [`../host_protocol.md`](../host_protocol.md)
