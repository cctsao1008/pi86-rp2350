# pi86-rp2350 Project Overview

## Project Identity

`pi86-rp2350` is not intended to be a board-for-board port of Pi86.

The project preserves the original Pi86 concept of using a **real NEC V20/V30 CPU** while replacing the Raspberry Pi/Linux/WiringPi host with a Waveshare RP2350-PiZero acting as a deterministic real-time bus engine and programmable chipset backend.

The project explores this hardware/software co-design model:

```text
BIOS / DOS / Applications
          |
          v
      NEC V30 CPU
          |
      physical x86 bus
          |
          v
  RP2350 real-time host
   |       |        |
   |       |        +-> virtual peripherals
   |       +----------> PIC / PIT / chipset services
   +------------------> memory / storage / display backends
```

## Why Replace Raspberry Pi 2/3?

The original Pi86 architecture is ingenious but its host execution model is fundamentally software-timed:

```text
Linux userspace
 -> WiringPi / GPIO access
 -> toggle V20/V30 clock
 -> sample control bus
 -> decode transaction
 -> provide memory or I/O response
```

The original project reports an operating point of approximately **0.3 MHz** for the physical processor. That figure is treated here as the historical comparison baseline.

The RP2350 is not expected to outperform Raspberry Pi 2/3 in general-purpose compute. The hypothesis is narrower and more relevant:

> A lower-clocked microcontroller with bare-metal deterministic execution, direct SIO access, PIO state machines, tightly coupled SRAM, and optional DMA can be a better host for a cycle-sensitive external V20/V30 bus than a much faster Linux application processor using software GPIO timing.

The replacement therefore targets **architecture**, not raw CPU benchmark performance.

## Central Research Question

> **How far can a modern deterministic microcontroller replace the chipset, memory, and peripheral infrastructure around a real NEC V20/V30 CPU while maintaining practical PC-class performance?**

This breaks down into several measurable engineering questions:

```text
Can RP2350 sustain a physical V30 at 1 MHz?
Can it sustain 2 MHz?
Can it reach the IBM PC-class 4.77 MHz operating point?
Can it approach the installed V30's 8 MHz class?

Can critical bus timing move from Cortex-M33 software loops into PIO?
Can DMA reduce repetitive host intervention without harming determinism?
Can external PSRAM satisfy memory-service deadlines?
Can MicroSD, DVI, USB and other slow services run without disturbing the bus-critical path?
Can BIOS and DOS execute on top of those virtualized peripherals?
```

## Mission

The project mission is:

> **Re-architect Pi86 into a deterministic RP2350-based physical-x86 platform that preserves the real V20/V30 CPU while overcoming the original Linux GPIO bus-performance bottleneck and progressing toward practical PC-class speed and compatibility.**

Functional compatibility is necessary, but it is not sufficient by itself. A successful DOS boot at a very low processor clock would prove compatibility but would not fully validate the reason for replacing the original host architecture.

## Why Waveshare RP2350-PiZero?

The board is useful because it combines the RP2350 real-time MCU architecture with a Raspberry Pi-style physical platform that fits the existing Pi86 HAT strategy.

### Mechanical and interface continuity

The Raspberry Pi 40-pin physical header remains the project hardware ABI:

```text
original Pi86 V20/V30 HAT
        |
        | Raspberry Pi physical 40-pin header
        v
Waveshare RP2350-PiZero
```

This avoids a new HAT PCB becoming part of the research problem. The existing physical CPU/HAT assembly can remain fixed while the host architecture changes underneath it.

### Platform integration direction

The board also provides a natural path toward an integrated system:

```text
                 NEC V30
                    |
             physical x86 bus
                    |
          +---------v---------+
          |       RP2350      |
          |                   |
          | PIO / SIO bus     |
          | PIC / PIT         |
          | memory backend    |
          | system services   |
          +----+----+----+----+
               |    |    |
             PSRAM SD   DVI/USB
```

That allows the RP2350 to evolve toward the role of a small programmable motherboard/chipset around a real x86 processor.

## Success Criteria

The project tracks three independent dimensions of success.

### 1. Functional success

```text
physical V30
 -> memory
 -> I/O
 -> PIC / PIT
 -> BIOS
 -> storage / keyboard / display services
 -> DOS
```

This answers: **Does the machine work?**

### 2. Architectural success

Required properties:

- no Linux scheduler dependency on the V30 bus-critical path;
- deterministic timing for critical bus phases;
- clear separation between hard-real-time V30 bus service and slower peripheral work;
- reusable memory, I/O, PIC and PIT backends;
- PIO/SIO/DMA used where measurement justifies offload;
- regressions remain testable through the existing gate chain.

This answers: **Is the replacement architecture technically better suited to the problem?**

### 3. Performance success

Performance must be measured on physical V30 hardware.

Initial interpretation targets:

| Physical V30 clock | Interpretation |
|---:|---|
| ~0.3 MHz | Historical original-Pi86 comparison baseline |
| >= 1 MHz | Minimum meaningful improvement |
| >= 2 MHz | Major improvement over the original reported baseline |
| 4.77 MHz | Primary target: IBM PC-class operating point |
| 8 MHz class | Stretch target for the installed V30 |

These are research thresholds, not claims of already-achieved performance.

## Performance Strategy

Performance work should occur before the project becomes deeply dependent on BIOS/DOS layers.

The intended sequence is:

```text
complete minimum timer/interrupt platform boundary
        |
        v
Performance Characterization 1
measure current architecture ceiling
        |
        +---------------------------+
        |                           |
        | ceiling sufficient        | ceiling insufficient
        v                           v
continue compatibility work     optimize bus engine
                                    |
                                    v
                         direct SIO / SRAM hot path
                                    |
                                    v
                            PIO-assisted timing
                                    |
                                    v
                         DMA where measurably useful
                                    |
                                    v
                           re-characterize ceiling
```

### Performance Characterization 1

The first formal benchmark should sweep increasing physical V30 clock targets, for example:

```text
1.0 MHz
2.0 MHz
2.5 MHz
3.0 MHz
4.0 MHz
4.77 MHz
6.0 MHz
8.0 MHz
```

The exact sweep may be adjusted to measured hardware behavior.

At each point, record at minimum:

- memory read/write correctness;
- byte-lane and odd-address correctness;
- I/O transaction correctness;
- interrupt acknowledge correctness;
- PIC/PIT behavior where applicable;
- sustained execution duration;
- error count;
- bus-service latency / timing margin where measurable;
- failure mode when the next frequency step becomes unstable.

The result must identify the **maximum sustainable validated V30 clock for the current architecture**, not merely the fastest clock that produces occasional instruction fetches.

## Development Evolution

### Phase 1 — CPU and Bus Bring-up

Validated:

- RP2350 firmware baseline;
- V30 reset sequence;
- physical bus validation;
- first instruction fetch.

### Phase 2 — Memory and I/O Subsystem

Validated:

- SRAM-backed execution;
- memory read/write;
- byte lanes and odd-address transactions;
- byte I/O transactions.

### Phase 3 — Interrupt Subsystem

Validated through Gate 11:

- maskable interrupt entry;
- reusable `pi86_pic` backend;
- programmable 8259A-compatible subset;
- ICW1-ICW4 initialization;
- IMR / IRR / ISR;
- fixed-priority arbitration;
- two INTA cycles;
- vector delivery;
- ISR blocking;
- non-specific EOI;
- sequential IRQ0/IRQ1 physical V30 execution with `IRET`.

### Phase 4 — Timer Boundary

Gate 12 introduces the minimal programmable PIT channel-0 path required to generate IRQ0 through the validated PIC architecture.

Controller-state PIT/PIC validation and physical-V30 validation are deliberately separated.

### Phase 5 — Performance Characterization and Bus Optimization

Before large BIOS/DOS expansion, establish the current bus-engine performance ceiling and decide whether the existing Cortex-M33/SIO architecture is sufficient or whether critical timing must move further into PIO/DMA.

### Phase 6 — PC-Compatible Platform Expansion

After the architecture has a measured performance baseline:

- periodic timer behavior required by the selected BIOS;
- BIOS POST and services;
- keyboard path;
- boot storage;
- DOS milestone;
- display integration;
- broader chipset compatibility as demanded by actual software dependencies.

## Design Principle

The project follows evidence-driven development:

1. Define a bounded capability or performance hypothesis.
2. Implement the minimum required behavior.
3. Validate on real V30/RP2350 hardware.
4. Capture measurable evidence.
5. Preserve regression evidence before expanding scope.
6. Refactor only when the measured architecture or dependency boundary justifies it.

The project is not a software-only emulator and not merely an MCU port. It is an experiment in building a practical physical-x86 platform around a modern deterministic microcontroller backend.
