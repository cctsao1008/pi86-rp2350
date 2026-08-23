# pi86-rp2350 Project Overview

## Project identity

`pi86-rp2350` is a programmable companion-chip platform built around a physical NEC V30, the original Pi86 V20/V30 HAT interface, and a Waveshare RP2350-PiZero.

The V30 executes native x86 code and owns architectural state. RP2350 PIO, DMA, SRAM, firmware, and external resources provide the surrounding clock, memory, I/O, peripheral, debug, and host-service environment.

This is not an RP2350 x86 emulator and not merely a board-for-board Pi86 port.

## Research objective

The central question is:

> How far can an RP2350 act as a deterministic, software-defined companion chipset around a real NEC V30 while retaining physical execution, useful PC-class compatibility, and independently verifiable evidence?

The work has two connected directions:

1. build a progressively more capable physical V30 computer;
2. build a provider-neutral bridge between native V30 software and modern host services.

## Why RP2350

The original Pi86 model used a Raspberry Pi and software-timed GPIO service. Its approximately 0.3 MHz physical-CPU operating point is an important historical baseline.

The RP2350 hypothesis is architectural rather than merely computational. PIO and DMA can own exact physical timing while Arm cores and the host handle slower policy and services outside the current bus cycle.

```text
PIO / DMA       = deterministic current-cycle data plane
Realtime role   = supervision and immutable publication
Service role    = USB, storage, trace, display, and formatting
Host software   = policy, tools, images, validation, optional AI
```

## Companion-chip structure

```text
BIOS / monitor / applications
             |
        physical NEC V30
             |
      multiplexed x86 bus
             |
 +------------------------------------+
 |               RP2350               |
 | PIO observation / response / clock |
 | DMA transport and retained trace   |
 | SRAM ROM, RAM, mailbox, device state|
 | realtime and service control planes|
 +---------+----------+---------------+
           |          |
      bulk memory   USB / SD / display
```

The physical Raspberry Pi 40-pin header position is the legacy hardware ABI. A future consolidated board may add buffering, voltage-domain handling, controllable READY, and a separate control connector without changing the architectural ownership rules.

## Capability domains

### Physical execution

- reset qualification and first fetch at `FFFF0h`;
- native far jump and internal-SRAM-backed ROM execution;
- CPU-visible checkpoints rather than transfer counters alone;
- controlled clock stop and safe terminal bus ownership.

### Memory

- qualified ROM and bounded RAM response;
- word, byte-lane, and odd-address semantics;
- internal SRAM for deterministic hot state;
- future PSRAM and storage behind explicit cache or READY contracts.

### I/O and interrupts

- native V30 I/O reads and writes;
- diagnostic and companion-service mailboxes;
- 8259A-compatible PIC behavior;
- interrupt acknowledge, IVT, ISR, EOI, and `IRET`;
- programmable timer and future PC-class device services.

### Host bridge

- provider-neutral fixed records over USB HID;
- receive-only CDC engineering evidence;
- asynchronous host service outside current-cycle timing;
- conventional programs, debuggers, Codex, ChatGPT, or another client above one Host Bridge API.

The V30 does not know what AI is. It sees only machine services expressed through I/O, memory, polling, interrupts, and native code. AI is an optional modern host-side adapter.

### BIOS and software platform

- reproducible native V30 ROM images;
- project diagnostic console and monitor services;
- progressively richer BIOS contracts;
- eventual storage, keyboard, display, boot-sector, and DOS/CP/M-86 exploration.

## Evidence classes

Performance and compatibility claims must name the architecture actually tested:

| Evidence class | Meaning |
|---|---|
| Fixed/prestaged | response sequence known before the epoch |
| Address-qualified | current physical address selects a bounded response |
| Bounded memory | validated finite ROM/RAM working set and access pattern |
| General memory | arbitrary supported addresses with defined miss behavior |
| Integrated system | ROM, RAM, I/O, interrupts, and sustained workload together |

A fixed self-loop at 8 MHz does not imply an integrated 8 MHz computer. A human-readable greeting does not by itself prove native V30 execution. Accepted evidence combines physical traces, CPU-visible effects, transport identity, deadline checks, and terminal electrical state.

## Bridge between eras

The physical V30 and a modern host were created roughly forty years apart and do not share a native vocabulary. `pi86-rp2350` provides the translation boundary:

```text
modern host / optional AI
          |
    structured records
          |
 RP2350 companion bridge
          |
 mailbox / I/O / memory / interrupt
          |
   physical NEC V30
```

The goal is not to rewrite the history of the V30 or pretend it understands modern AI. The goal is to let both eras exchange verifiable work while each remains native to its own execution model.

## Research challenge for engineering agents

`pi86-rp2350` is deliberately a physically grounded challenge. It does not
measure an agent by the amount of code or prose it can produce. It measures
whether proposed reasoning survives contact with an independently behaving
processor and a retained bus trace.

The strongest agent-facing problems combine several boundaries:

- derive a timing hypothesis from datasheets and schematics;
- implement it across PIO, DMA, M33 firmware, V30 assembly, and host tools;
- predict the first observable distinction between true execution and a false positive;
- recover from failed physical evidence without weakening the acceptance gate;
- generate fresh challenges or code capsules whose answers were not prestaged;
- explain the first divergence between known-good and failing bus histories.

This makes the physical V30 more than a demonstration endpoint. It is an
external evaluator: the agent may propose, implement, and interpret, but the
hardware decides whether the claim is true.

## Engineering principles

1. Define a bounded CPU-visible capability.
2. Keep current-cycle timing in PIO/DMA.
3. Publish only complete immutable state.
4. Separate application traffic from physical evidence.
5. Preserve unsupported cycles as safe high-Z behavior.
6. State target architecture separately from validated capability.
7. Record failures as evidence rather than hiding them behind a visible result.
8. Advance compatibility according to BIOS and workload dependencies.

## Document map

- [`../README.md`](../README.md) - main project purpose and architecture
- [`README.md`](README.md) - documentation index
- [`architecture.md`](architecture.md) - timing, ownership, memory, and I/O architecture
- [`ai_bridge_architecture.md`](ai_bridge_architecture.md) - provider-neutral host bridge and V30 companion-service boundary
- [`hardware_contract.md`](hardware_contract.md) - canonical physical interface
- [`native_bios_architecture.md`](native_bios_architecture.md) - native BIOS structure
- [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) - dependency-driven PC compatibility scope
- [`validation/`](validation/) - immutable physical evidence and acceptance records
- [`adr/`](adr/) - architecture decisions and their consequences
