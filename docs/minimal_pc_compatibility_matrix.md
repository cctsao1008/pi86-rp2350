# Pi86-RP2350 Minimal PC Compatibility Dependency Matrix

## Purpose

Define the minimum functional path from the currently validated V30 memory bus to a BIOS that can boot DOS-class software, without implementing unrelated PC hardware too early.

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
| Refactored reusable V30 bus + memory backend | PASS | Gate 6R |
| Byte lanes and odd-address word transactions | IN VALIDATION | Gate 7 |

Gate 7 remains a prerequisite for claiming a general byte-addressed x86 memory subsystem.

## Dependency chain

```text
V30 byte-addressed memory bus
        |
        v
I/O port read/write transactions
        |
        +------------------------+
        |                        |
        v                        v
basic debug/POST output      programmable interrupt path
                                 |
                                 v
                              PIC / INTA
                                 |
                                 v
                              timer IRQ
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

Video can initially be decoupled from the hard real-time V30 bus path if BIOS console output is provided through a minimal compatible abstraction or an existing Pi86 virtual-CGA mechanism. Full display compatibility is a separate validation track.

## Proposed gate sequence after Gate 7

### Gate 8 — I/O port transaction primitive

**Goal:** Prove V30 `IN` and `OUT` instructions reach an RP2350 I/O backend with correct port address, direction and data.

Minimum test:

- execute `OUT` to a project-owned diagnostic port;
- backend records the value;
- execute `IN` from a project-owned diagnostic port;
- backend returns a known value;
- CPU compares returned value and branches to SUCCESS.

Acceptance must be CPU-semantic, not merely host-side observation.

Why required: BIOS initialization and virtually all PC peripherals are I/O-port driven.

### Gate 9 — interrupt acknowledge / PIC skeleton

**Goal:** Prove the complete CPU interrupt path, including asserted `INTR`, V30 interrupt-acknowledge bus cycle(s), vector delivery and execution of an ISR.

Initial implementation may use a minimal software PIC model rather than a complete 8259A feature set.

Acceptance:

- RP2350 asserts interrupt request;
- V30 performs expected INTA sequence;
- backend supplies vector;
- V30 reaches known ISR;
- ISR changes a RAM sentinel or branches to a success loop;
- no polling shortcut is allowed in the acceptance path.

Why required: PC/XT BIOS timer and keyboard services depend on maskable interrupts.

### Gate 10 — PIT-compatible timer subset

**Goal:** Provide the smallest 8253/8254 behavior required for BIOS timebase and timer IRQ operation.

Initial scope:

- I/O programming interface needed by the selected BIOS path;
- channel behavior sufficient to produce periodic IRQ0;
- deterministic RP2350 implementation.

Do not implement unused PIT modes until a BIOS or DOS dependency requires them.

### Gate 11 — minimal BIOS POST entry

**Goal:** Replace synthetic test ROM with a minimal BIOS image that reaches a deterministic POST checkpoint using the validated memory and I/O services.

Possible implementation references:

1. `skiselev/8088_bios` for XT-class behavior and dependency discovery;
2. TinyBIOS for minimal skeleton/flow;
3. `maxmalysh/simple-bios` for educational comparison;
4. IBM PC/XT BIOS listings for historical contract comparison.

Acceptance should identify a CPU-visible checkpoint, not merely that the BIOS binary was fetched.

### Gate 12 — keyboard service subset

**Goal:** Provide enough keyboard-facing behavior for BIOS keyboard initialization and INT 16h input.

Implementation choice should be driven by the selected BIOS configuration. Full XT/AT/PS2 controller emulation is not automatically required for the first DOS boot.

### Gate 13 — boot-storage / INT 13h subset

**Goal:** Read a boot sector from an RP2350-managed disk image through a BIOS-visible disk service.

Potential backend: onboard MicroSD. MicroSD transactions must remain outside the hard real-time V30 bus service path; buffering/caching should isolate slow storage from CPU bus deadlines.

Acceptance:

- BIOS requests sector 0;
- RP2350 returns exact 512-byte boot sector;
- BIOS transfers control to loaded boot code;
- CPU reaches a known sentinel in the boot sector.

### Gate 14 — DOS boot milestone

**Goal:** Boot a deliberately selected DOS image to a deterministic milestone.

Initial acceptance should be narrower than 'fully compatible DOS PC'. Examples:

- DOS kernel begins execution;
- command interpreter starts;
- a known program executes and writes a sentinel.

The exact criterion must be selected before the gate is implemented.

## Peripheral priority

| Subsystem | Initial priority | Reason |
|---|---:|---|
| Memory byte lanes / odd access | P0 | Required for normal x86 execution |
| I/O port backend | P0 | Gateway to PC peripheral model |
| Interrupt acknowledge / PIC subset | P0 | Required for timer/keyboard IRQ architecture |
| PIT timer subset | P0 | BIOS timebase / IRQ0 dependency |
| Boot-storage service | P0 | Required to load an OS |
| Keyboard service | P1 | Required for interactive DOS, but can follow first noninteractive boot experiments depending on BIOS strategy |
| Minimal console/video path | P1 | Needed for observable BIOS/DOS UI; can be developed partly out of the hard real-time bus path |
| DMA | P1/P2 | Implement when selected floppy/device path proves it is required |
| RTC | P2 | Useful but not necessary for first boot milestone |
| Serial / printer | P2 | BIOS compatibility feature, not first-boot prerequisite |
| Expansion ROM scanning | P2 | Needed for broader PC compatibility, not minimum boot path if integrated BIOS services are used |
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

This matrix is intentionally provisional beyond the currently validated gates. A future gate may be reordered when direct BIOS evidence shows a different dependency. Such changes should update this document with the evidence and rationale rather than silently changing the development sequence.
