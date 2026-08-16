# pi86-rp2350 Project Overview

## Project identity

`pi86-rp2350` is an RP2350-based **V30 companion-chip platform** built around a real NEC V20/V30 and the original Pi86 CPU HAT.

It began by re-architecting Pi86 away from Raspberry Pi/Linux/WiringPi GPIO timing. Physical bring-up and PC1-B performance evidence now support a broader definition: the RP2350 is the programmable chipset that provides bus control, memory, peripherals, storage, display integration, and debugging around the V30.

```text
monitor / BIOS / applications
             |
             v
        physical NEC V30
             |
      multiplexed x86 bus
             |
             v
       RP2350 companion chip
        |       |       |
      memory   devices  debugger/services
        |       |       |
      PSRAM   SD/DVI   USB/keyboard
```

This is neither a software-only emulator nor a board-for-board Pi86 clone. The V30 executes the instructions and owns x86 architectural state; the RP2350 replaces the surrounding historical chipset with deterministic programmable logic and firmware.

## Why replace the Raspberry Pi 2/3 host?

The original Pi86 execution model is software-timed:

```text
Linux userspace
 -> GPIO library
 -> toggle CPU clock
 -> sample and decode bus
 -> provide memory or I/O response
```

Its reported physical CPU operating point of approximately 0.3 MHz is the historical comparison baseline.

The RP2350 hypothesis is architectural rather than computational: a bare-metal MCU with PIO, DMA, tightly coupled SRAM, and bounded execution can be a better real-time bus companion than a faster application processor behind a general-purpose operating system.

## Central research question

> How far can an RP2350 replace the chipset, memory, and peripheral infrastructure around a real NEC V30 while retaining practical PC-class speed and compatibility?

The project now divides that question into explicit evidence boundaries:

1. Can the RP2350 generate and observe the physical bus correctly?
2. Can PIO directly meet the V30 read-response window at real V30-class clocks?
3. Can captured addresses select real ROM/RAM data before the fixed READY deadline?
4. Can RAM, byte lanes, I/O, PIC, PIT, and interrupt acknowledge be reintegrated under continuous clock?
5. Can a monitor and minimal BIOS expose useful services?
6. Can storage, keyboard, display, and a boot sector support DOS-class or CP/M-86 software?

## Current evidence

### Functional gate chain

Physical hardware validation has established:

- reset and first fetch at `0xFFFF0`;
- SRAM-backed executable ROM and far jump to `0xF0000`;
- RAM writes, reads, comparisons, and CPU-visible branches;
- byte lanes and odd-address word transactions;
- I/O-space transactions;
- maskable interrupt entry and two INTA cycles;
- reusable 8259A-compatible PIC behavior;
- multi-IRQ priority, ISR blocking, EOI, and `IRET`;
- programmable PIT channel 0 reaching IRQ0 through the PIC path.

These gates were primarily validated with software-stepped bus service. They remain semantic regression evidence while the continuous-clock architecture is built.

### PC1-B performance result

PC1-B moved the critical read response out of M33/SIO software and into:

```text
SRAM -> DMA -> PIO1 TX FIFO -> OUT pins, 28 -> PINDIRS -> V30 AD bus
```

A post-reset `EB FE` self-loop discriminator passed at every configured point:

| Configured V30 clock | Result |
|---:|---|
| 0.300 MHz | PASS |
| 0.600 MHz | PASS |
| 1.200 MHz | PASS |
| 2.000 MHz | PASS |
| 3.000 MHz | PASS |
| 4.000 MHz | PASS |
| 5.000 MHz | PASS |
| 6.000 MHz | PASS |
| 7.000 MHz | PASS |
| 8.000 MHz | PASS |

The repeated `FFFF0` after reset prefetch proves the V30 consumed and executed the PIO-driven instruction. Default input synchronizers remained enabled.

### What PC1-B does not prove

The response data was fixed and pre-staged. PC1-B therefore does not establish:

- arbitrary address-dependent ROM/RAM lookup at 8 MHz;
- qualified memory versus I/O ownership;
- dynamic byte-lane responses;
- write capture under the continuous-clock engine;
- integrated PIC/PIT/INTA behavior at 8 MHz;
- PSRAM cache-miss behavior;
- sustained general workload execution.

Those distinctions are mandatory in all performance claims.

## Active milestone: PC1-C ROM execution

PC1-C converts the proven timing front-end into an address-qualified memory path.

### PC1-C0

```text
FFFF0: JMP FAR F000:0000
                 |
                 v
F0000: deterministic ROM program
                 |
                 v
CPU-visible checkpoint
```

Acceptance requires data to be selected from the captured address and cycle type. A response stream indexed only by transaction count is not accepted as a general ROM backend.

### PC1-C1

The first observable Mini BIOS service is a project debug port, initially `0xE9`. ROM code writes a short signature and the RP2350 mirrors it to USB CDC:

```text
V30 ROM -> OUT 0E9h, AL -> RP2350 I/O backend -> USB CDC
```

This provides an end-to-end boot signature without making UART, OLED, or CGA a prerequisite.

## Companion-chip partitioning

### PIO and DMA data plane

- continuous clock and controlled LOW stop;
- passive ALE/address/control capture;
- direct encoded AD output and `PINDIRS` ownership;
- SRAM-to-PIO FIFO movement;
- deterministic event capture.

### Real-time M33 role

- address/control decode;
- deterministic response-queue or cache supervision;
- memory-write and exceptional-cycle handling;
- deadline/starvation telemetry;
- no blocking service-layer calls.

### Service M33 role

- USB debugger and monitor control;
- ROM/disk/configuration images;
- MicroSD;
- keyboard and display work;
- host-visible diagnostics.

The logical roles are fixed; their Core 0/Core 1 assignment is not locked until shared-resource contention is measured.

## Hardware constraint: READY

The original HAT connects V30 `READY` to 3.3 V. The current interface cannot insert wait states.

This is a first-class architecture constraint:

- dynamic lookups have a hard response deadline;
- PSRAM and MicroSD require deterministic caching or prefetch;
- a miss cannot be hidden by pausing the current CPU cycle;
- PC1-C must measure the first dynamic-lookup failure point independently of PC1-B.

A future hardware revision may expose READY, but the current project must not assume that capability.

## Success dimensions

### Functional

```text
physical V30
 -> address-qualified ROM/RAM
 -> I/O and interrupt devices
 -> monitor / BIOS
 -> storage / keyboard / display
 -> bootable operating system
```

### Architectural

- no operating-system scheduler in the bus-critical path;
- deterministic PIO/DMA ownership of physical timing;
- bounded real-time supervision;
- slower services isolated behind queues and caches;
- reusable memory and device backends;
- explicit safe high-Z terminal states;
- regression evidence preserved as the engine evolves.

### Performance

- fixed/pre-staged response: validated through 8.000 MHz;
- address-qualified internal-SRAM ROM: active PC1-C measurement;
- integrated ROM/RAM/I/O/interrupt engine: future measurement;
- external PSRAM and full system: future measurement.

The project targets authentic stable V30-class operation. It does not pursue clock rate beyond the installed 8 MHz-grade CPU merely for a headline number.

## Development roadmap

```text
DONE
  Gate 0-12 semantic chain
  PC1-B PIO-direct fixed response, 0.300-8.000 MHz

ACTIVE
  PC1-C0 address-qualified far-jump ROM

NEXT
  PC1-C1 debug-port Mini BIOS signature
  PC1-D deterministic RAM read/write
  PC1-E ROM monitor
  PC1-F minimum BIOS services
  PC1-G boot-sector and DOS/CP/M-86 exploration
```

Compatibility work remains dependency-driven. A peripheral is implemented when a monitor, BIOS, or boot milestone needs it, not simply because the original IBM PC contained it.

## Hardware platform

- Host: Waveshare RP2350-PiZero
- CPU: NEC V30 `D70116C-8` / `uPD70116C-8`
- CPU HAT: original Homebrew8088 Pi86 V20/V30 HAT
- Hardware ABI: Raspberry Pi physical 40-pin header position
- Bring-up memory: internal RP2350 SRAM
- Planned system memory: APS6404L-class 8 MB PSRAM
- Storage: onboard MicroSD
- Display direction: onboard DVI with virtual CGA memory
- Debug path: native USB CDC

The installed CPU is nominally a 5 V part operated by a HAT designed around 3.3 V. This remains a project-specific empirical condition rather than a change to NEC's nominal rating.

## Decision and evidence sources

- [`architecture.md`](architecture.md) — current companion-chip structure and timing boundaries.
- [`adr/0002-adopt-v30-companion-chip-architecture.md`](adr/0002-adopt-v30-companion-chip-architecture.md) — architecture decision.
- [`hardware_contract.md`](hardware_contract.md) — canonical physical interface mapping.
- [`validation/pc1b_pio_direct_frequency_sweep.md`](validation/pc1b_pio_direct_frequency_sweep.md) — PC1-B result.
- [`pc1c_rom_execution_plan.md`](pc1c_rom_execution_plan.md) — active ROM milestone.
- [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) — dependency-driven route toward a bootable machine.

## Development principle

1. Define a bounded CPU-visible capability.
2. Implement the minimum behavior that can prove it.
3. Validate on the physical V30.
4. Record the exact architecture and performance class tested.
5. Preserve the last known-good baseline.
6. Advance only when the current dependency is closed by evidence.
