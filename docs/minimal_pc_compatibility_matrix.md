# Pi86-RP2350 Minimal PC Compatibility Dependency Matrix

## Purpose

Define the minimum functional path from the currently validated V30 bus and interrupt subsystem toward a BIOS that can boot DOS-class software, without implementing unrelated PC hardware too early.

This document is a planning and dependency artifact. It does not change the normative source hierarchy for V30 electrical/bus behavior.

## Guiding rule

Each new gate should unlock a concrete BIOS or DOS dependency. Avoid implementing peripherals merely because they existed in an IBM PC/XT if the current boot path does not yet require them.

## Current validated baseline

| Capability | Status | Evidence boundary |
|---|---|---|
| Reset and first fetch at physical `0xFFFF0` | PASS | Gate 3 |
| Aligned 16-bit memory read | PASS | Gate 4 |
| SRAM-backed executable ROM | PASS | Gate 5 |
| Aligned RAM write/readback with CPU semantic branch | PASS | Gate 6 |
| Reusable V30 bus + byte-addressed memory backend | PASS | Gate 6R |
| Byte lanes and odd-address word transactions | PASS | Gate 7 |
| Byte I/O port transactions | PASS | Gate 8 |
| Physical maskable interrupt entry | PASS | Gate 9 |
| Reusable PIC regression backend | PASS | Gate 9R |
| Programmable 8259A-compatible PIC subset | PASS | Gate 10 |
| Multi-IRQ fixed priority, ISR blocking, EOI recovery, `IRET` | PASS | Gate 11 |
| Programmable PIT channel-0 to IRQ0 | PENDING | Gate 12 |

## Dependency chain

```text
V30 byte-addressed memory bus
        |
        v
I/O port read/write transactions
        |
        v
programmable interrupt path
        |
        v
PIC / INTA / IVT / ISR / EOI / IRET
        |
        v
programmable timer IRQ0
        |
        v
BIOS timing services
        |
        +------------------------+
        |                        |
        v                        v
keyboard input              boot-storage service
        |                        |
        v                        v
BIOS INT 16h               BIOS INT 13h
        |                        |
        +------------+-----------+
                     |
                     v
                boot sector
                     |
                     v
                  DOS
```

Video can initially be decoupled from the hard real-time V30 bus path if BIOS console output is provided through a minimal compatible abstraction or an existing Pi86 virtual-CGA mechanism. Full display compatibility remains a separate validation track.

## Actual validated gate sequence

### Gate 8 — I/O port transaction primitive

**Status: PASS**

Physical V30 `IN`/`OUT` byte transactions on even/odd ports are validated.

### Gate 9 — Interrupt acknowledge / vector entry

**Status: PASS**

Physical V30 maskable interrupt entry through two INTA cycles, vector acquisition, IVT lookup and ISR execution is validated.

### Gate 9R — Reusable PIC regression

**Status: PASS**

The Gate 9 behavior was reproduced through reusable `pi86_pic` state without introducing new PIC programming semantics.

### Gate 10 — Programmable 8259A-compatible PIC subset

**Status: PASS**

Validated CPU-visible ICW1-4 programming, IMR, IRR, ISR, IRQ0, fixed-priority baseline, vector derivation, two-INTA behavior and non-specific EOI on physical V30 hardware.

### Gate 11 — Multi IRQ fixed-priority validation

**Status: PASS**

Validated IRQ0/IRQ1 arbitration, ISR blocking, pending lower-priority recovery, two sequential physical interrupt entries, EOI and `IRET`.

The decisive sequence was:

```text
IRQ1 pending
IRQ0 pending
 -> IRQ0 vector 20h
 -> ISR0 / marker A0h / EOI / IRET
 -> pending IRQ1 becomes serviceable
 -> IRQ1 vector 21h
 -> ISR1 / marker A1h / EOI / IRET
 -> IRR=00h ISR=00h INTR=0
```

## Active boundary

### Gate 12 — Minimal programmable PIT channel 0 -> IRQ0

**Status: DEFINED — IMPLEMENTATION / HARDWARE VALIDATION PENDING**

**Goal:** provide the smallest CPU-programmed PIT-compatible channel 0 path that raises IRQ0 through the already validated PIC and physical V30 interrupt path.

Required chain:

```text
OUT 43h / OUT 40h
  -> PIT channel 0
  -> terminal-count event
  -> pi86_pic IRQ0
  -> INTR
  -> INTA #1 / #2
  -> vector 20h
  -> IVT
  -> ISR0
  -> marker
  -> EOI
  -> IRET
  -> SUCCESS
```

Initial implementation deliberately uses a deterministic one-shot-style validation path. Full periodic BIOS tick behavior is deferred until the PIT-to-PIC dependency has passed on physical hardware.

## Provisional sequence after Gate 12

The following numbering is provisional and must be updated when a concrete BIOS dependency changes the order.

### Gate 13 — Periodic timer / BIOS timing contract

Goal: expand the validated Gate 12 timer path only as far as required by the selected BIOS timing dependency.

Potential scope:

- periodic channel-0 operation required by the selected BIOS;
- repeated IRQ0 delivery;
- timer ISR state accumulation;
- deterministic long-run validation.

Do not automatically implement every 8253/8254 mode.

### Gate 14 — Minimal BIOS POST entry

Goal: replace synthetic test ROM with a minimal BIOS image that reaches a deterministic POST checkpoint using the validated memory, I/O, PIC and timer services.

Possible implementation references:

1. `skiselev/8088_bios` for XT-class behavior and dependency discovery;
2. TinyBIOS for minimal skeleton/flow;
3. `maxmalysh/simple-bios` for educational comparison;
4. IBM PC/XT BIOS listings for historical contract comparison.

Acceptance should identify a CPU-visible checkpoint, not merely that the BIOS binary was fetched.

### Gate 15 — Keyboard service subset

Goal: provide enough keyboard-facing behavior for BIOS keyboard initialization and INT 16h input.

Implementation choice must be driven by the selected BIOS configuration. Full XT/AT/PS/2 controller emulation is not automatically required for the first DOS boot.

### Gate 16 — Boot-storage / INT 13h subset

Goal: read a boot sector from an RP2350-managed disk image through a BIOS-visible disk service.

Potential backend: onboard MicroSD. MicroSD transactions must remain outside the hard real-time V30 bus service path; buffering/caching should isolate slow storage from CPU bus deadlines.

Acceptance:

- BIOS requests sector 0;
- RP2350 returns exact 512-byte boot sector;
- BIOS transfers control to loaded boot code;
- CPU reaches a known sentinel in the boot sector.

### Gate 17 — DOS boot milestone

Goal: boot a deliberately selected DOS image to a deterministic milestone.

Initial acceptance should be narrower than "fully compatible DOS PC". Examples:

- DOS kernel begins execution;
- command interpreter starts;
- a known program executes and writes a sentinel.

The exact criterion must be selected before implementation.

## Peripheral priority

| Subsystem | Initial priority | Reason |
|---|---:|---|
| Memory byte lanes / odd access | DONE | Required for normal x86 execution |
| I/O port backend | DONE | Gateway to PC peripheral model |
| Interrupt acknowledge / PIC subset | DONE | Required for timer/keyboard IRQ architecture |
| Multi-IRQ PIC priority | DONE | Required for credible interrupt-controller behavior |
| PIT timer subset | P0 | BIOS timebase / IRQ0 dependency |
| Boot-storage service | P0 | Required to load an OS |
| Keyboard service | P1 | Required for interactive DOS; may follow first noninteractive boot experiments |
| Minimal console/video path | P1 | Needed for observable BIOS/DOS UI; can develop partly outside hard real-time bus path |
| DMA | P1/P2 | Implement when selected device path proves it is required |
| RTC | P2 | Useful but not necessary for first boot milestone |
| Serial / printer | P2 | BIOS compatibility feature, not first-boot prerequisite |
| Expansion ROM scanning | P2 | Needed for broader compatibility, not minimum boot path if integrated BIOS services are used |
| Full VGA/EGA | P3 | Outside first minimal DOS boot objective |
| Full chipset emulation | P3 | Avoid unless a concrete software dependency requires it |

## BIOS strategy options

### Option A — Project-specific minimal BIOS

Advantages:

- smallest dependency surface;
- easiest to align with RP2350 virtual peripherals;
- fastest route to a controlled boot milestone.

Risks:

- may hide compatibility gaps that a real XT BIOS would expose;
- later migration to broader software compatibility may require rework.

### Option B — Adapt an XT-class BIOS such as `skiselev/8088_bios`

Advantages:

- real PC/XT BIOS behavior;
- existing POST and BIOS services;
- clear route toward DOS/software compatibility;
- existing NEC V20-oriented configurations are especially relevant as implementation references.

Risks:

- wider hardware dependency surface;
- more I/O devices may need to exist before POST advances;
- adaptation must respect upstream license obligations.

### Recommended staged strategy

Use a project-specific diagnostic/minimal BIOS first to validate infrastructure, while using `skiselev/8088_bios` to define compatibility requirements. Do not declare BIOS compatibility until a real XT-class BIOS path is separately validated.

## Source hierarchy for this roadmap

1. Actual verified Pi86-RP2350 hardware behavior.
2. NEC V20/V30 documentation for CPU/bus semantics.
3. Project hardware and architecture contracts.
4. Original Pi86 implementation for compatibility behavior.
5. IBM PC/XT technical documentation for PC-compatible machine contracts.
6. `skiselev/8088_bios` as the primary practical XT-class BIOS implementation reference.
7. TinyBIOS / simple-bios / SeaBIOS as secondary implementation comparisons.
8. General x86 educational material as background only.

## Decision boundary

This matrix remains provisional beyond the currently validated gates. A future gate may be reordered when direct BIOS evidence shows a different dependency. Such changes must update this document with the evidence and rationale rather than silently changing the development sequence.
