# pi86-rp2350 Project Overview

## Project Identity

`pi86-rp2350` is an RP2350-based x86 compatible platform using a real NEC V30 CPU and RP2350 as a programmable chipset backend.

The project explores hardware/software co-design:

```text
BIOS / DOS / Applications
          |
          v
      NEC V30 CPU
          |
      x86 Bus
          |
          v
 RP2350 Platform Controller
          |
          v
 x86-compatible chipset services
```

## Development Evolution

### Phase 1 - CPU and Bus Bring-up

- RP2350 firmware baseline
- V30 reset sequence
- Physical bus validation
- First instruction fetch

### Phase 2 - Memory and I/O Subsystem

- SRAM-backed execution
- Memory read/write validation
- Byte I/O transactions

### Phase 3 - Interrupt Subsystem

Current milestone:

- Gate 9: maskable interrupt entry
- Gate 9R: reusable `pi86_pic` backend
- Gate 10: 8259A-compatible programmable PIC subset

Validated capabilities:

- ICW1-ICW4 initialization
- IMR
- IRR
- ISR
- Fixed priority IRQ handling
- Two INTA cycles
- Vector delivery
- Non-specific EOI

### Phase 4 - PC-Compatible Platform Expansion

Future areas:

- PIT timer
- BIOS services
- DOS compatibility
- Additional chipset peripherals

## Design Principle

The project follows evidence-driven development:

1. Define a bounded capability.
2. Implement the minimum required behavior.
3. Validate on real V30/RP2350 hardware.
4. Preserve regression evidence before expanding scope.

The goal is not a software-only emulator. The goal is a real x86-compatible hardware platform built around a modern microcontroller backend.
