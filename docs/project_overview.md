# pi86-rp2350 Project Overview

## Project identity

`pi86-rp2350` is a programmable companion-chip platform built around a physical NEC V30, the original Pi86 V20/V30 HAT, and a Waveshare RP2350-PiZero.

The V30 executes native x86-class code and owns architectural state. The RP2350 supplies the programmable machine around it through PIO, DMA, deterministic on-chip state, firmware, and host-side tooling.

This is not an x86 emulator and not merely a board-for-board Pi86 port.

## Core concept

The project is organized around four ideas:

- **Physical** — the NEC V30 remains the processor executing native code.
- **Cycle-aware** — bus timing, ownership, and response behavior are explicit.
- **Programmable** — memory, I/O, interrupts, runtime services, and compatibility behavior can be supplied by the RP2350.
- **AI-operable** — modern tools and AI agents can observe, configure, and analyze the machine through structured host-side interfaces without entering the realtime bus path.

The central architectural boundary is:

> **PIO/DMA handle current-cycle V30 timing; Arm software and host tools operate around that realtime path.**

## System structure

```text
Host tools / AI
       |
Observe / Control / Experiment
       |
     RP2350
  PIO / DMA / services
       |
 Original Pi86 HAT
       |
 Physical NEC V30
```

PIO and DMA own qualified current-cycle capture and response. Arm software prepares and supervises bounded state, while asynchronous services such as USB, storage, trace processing, and higher-level tooling remain outside the active V30 bus cycle.

Internal SRAM is used for deterministic hot state. PSRAM and persistent storage are treated as backing/workspace unless a separately validated bounded response contract exists.

The existing Pi86 HAT remains the hardware baseline. Its `READY` signal is fixed high, so unsupported or late current-cycle responses must not silently depend on slow backing storage.

## Observe, control, and experiment

The host-facing interface exposes three generic capabilities:

- **Observe** — machine-readable bus activity, machine state, timing metadata, and runtime state.
- **Control** — bounded operations such as reset/run control, supported configuration, image selection, trace control, and safe state queries.
- **Experiment** — repeatable configuration, execution, comparison, and analysis on the physical V30.

AI is one possible host-side client. It can reason over structured state, request bounded operations, compare runs, and analyze failures. It is not part of current-cycle bus timing.

The same interfaces are intended to remain usable by conventional host software without requiring an AI service.

## Compatibility and workloads

PC-class behavior is useful as a compatibility profile and workload set rather than as the definition of the project.

Examples include BIOS code, interrupt/timer devices, RAM/ROM machine profiles, storage, display, keyboard, DOS, ELKS, diagnostic ROMs, and other native V30 software.

These workloads are valuable because they exercise increasingly complex interactions between the physical CPU and the programmable chipset while leaving the core architecture independent of any single PC/XT-compatible endpoint.

## Host bridge

The Host Bridge is the provider-neutral transport and translation layer between modern host software and V30-visible companion services.

To the V30, the RP2350 exposes ordinary machine mechanisms such as I/O ports, memory, polling, interrupts, and mailbox state. Provider-specific concepts such as Codex, ChatGPT, prompts, sessions, or credentials remain above the Host Bridge boundary.

The validated HID/CDC bridge and greeting experiments are retained as historical evidence of one implementation path. They do not define the project identity or limit future host interfaces.

## Hardware baseline

The current platform consists of:

- Waveshare RP2350-PiZero;
- Raspberry Pi RP2350B;
- physical NEC V30 `D70116C-8` / `uPD70116C-8`;
- original Homebrew8088 Pi86 V20/V30 HAT;
- Raspberry Pi-compatible 40-pin mechanical interface;
- onboard Flash plus optional PSRAM backing/workspace;
- native USB for console, control, bridge, and tooling services.

The existing Pi86 HAT remains the working hardware baseline unless a demonstrated architectural limitation requires reconsideration.

## Project lineage

`pi86-rp2350` builds directly on the Homebrew8088 Pi86 project and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor and service memory and I/O transactions in software.

`pi86-rp2350` preserves that physical CPU/HAT concept while moving the timing-critical boundary into RP2350 PIO, DMA, and deterministic on-chip state, then exposing the machine to modern host tooling and AI-assisted experimentation.

## Document map

- [`../README.md`](../README.md) - project introduction
- [`README.md`](README.md) - documentation index
- [`architecture.md`](architecture.md) - detailed timing, ownership, memory, and host-interface architecture
- [`dual_core_partitioning.md`](dual_core_partitioning.md) - realtime/service ownership model
- [`ai_bridge_architecture.md`](ai_bridge_architecture.md) - provider-neutral Host Bridge architecture
- [`companion_service_abi.md`](companion_service_abi.md) - host record and V30 companion-service ABI
- [`hardware_contract.md`](hardware_contract.md) - canonical physical interface
- [`validation/`](validation/) - historical physical validation records
- [`adr/`](adr/) - architecture decisions and their consequences
