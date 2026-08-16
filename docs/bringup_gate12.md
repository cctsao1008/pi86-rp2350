# Gate 12 — Minimal Programmable PIT IRQ0 Validation

## Objective

Introduce the smallest programmable PIT-compatible timer path that can raise IRQ0 through the already hardware-validated `pi86_pic` controller and physical NEC V30 interrupt path.

Gate 12 is intentionally narrower than full IBM PC/XT PIT compatibility. The purpose is to validate the dependency chain:

```text
V30 programs PIT
    -> PIT channel 0 counts
    -> timer output event
    -> pi86_pic IRQ0 request
    -> INTR
    -> two INTA cycles
    -> vector 20h
    -> IVT
    -> ISR0
    -> EOI
    -> IRET
```

## Initial Scope

Included:

- PIT control port `43h`
- PIT channel 0 data port `40h`
- channel 0 only
- one deterministic count format selected for this gate
- a minimal one-shot terminal-count behavior
- deterministic RP2350-side timer progression
- IRQ0 generation only through `pi86_pic_raise_irq(..., 0)`
- existing Gate 10/11 PIC programming contract
- physical V30 INTA / IVT / ISR / EOI / IRET execution

Explicitly excluded:

- PIT channels 1 and 2
- speaker behavior
- DRAM-refresh compatibility behavior
- full 8253/8254 mode matrix
- read-back command
- latch/read semantics beyond what this gate requires
- periodic BIOS tick compatibility
- BIOS time-of-day services
- timer calibration / final clock-rate optimization

## Core Validation

Target: `gate12_pit_core`

The PIT/PIC controller-state path has passed deterministic core validation:

```text
PIT control 43h = 30h
PIT channel 0 reload = 0004h
no terminal-count event before the final tick
terminal count produces exactly one event
terminal count -> pi86_pic IRQ0
PIC IRR = 01h
PIC INTR asserted
IRQ0 two-cycle acknowledge -> vector 20h
PIC ISR = 01h
non-specific EOI
final PIC ISR = 00h, INTR deasserted
```

This validates the PIT state machine and PIT-to-PIC integration only. It does not close Gate 12.

## Proposed CPU-Visible Test

1. Initialize PIC with vector base `20h`.
2. Unmask IRQ0 only (`IMR=FEh`).
3. Program PIT channel 0 through ports `43h` and `40h` with a deterministic test count.
4. Enable interrupts and enter a wait loop.
5. PIT backend reaches terminal count and raises IRQ0 through `pi86_pic`.
6. Physical V30 performs exactly two INTA cycles.
7. INTA #2 supplies vector `20h`.
8. V30 resolves IVT vector `20h` and executes ISR0.
9. ISR0 writes a known marker, sends non-specific EOI to port `20h`, and returns with `IRET`.
10. CPU reaches a deterministic SUCCESS loop.

## Architectural Rule

The PIT must not bypass the interrupt controller.

Forbidden acceptance shortcut:

```text
PIT event -> direct v30_bus_set_intr()
```

Required path:

```text
PIT event
  -> pi86_pic_raise_irq(&pic, 0)
  -> pi86_pic_intr_asserted(&pic)
  -> V30 INTR pin
```

This preserves the Gate 10/11 PIC contract as a regression invariant.

## Acceptance Criteria

- V30 emits the expected PIT programming I/O writes on ports `43h` / `40h`.
- PIT backend accepts the selected channel-0 programming sequence.
- IRQ0 is not raised before the programmed terminal-count event.
- terminal count raises exactly one IRQ0 request for this gate.
- IRQ0 request appears in PIC IRR and asserts INTR while unmasked.
- physical V30 performs exactly two INTA cycles for the timer interrupt.
- INTA #2 supplies vector `20h`.
- IRQ0 moves IRR -> ISR at acknowledge.
- V30 reads IVT vector `20h` and enters the expected ISR.
- ISR writes the expected RAM marker.
- non-specific EOI clears ISR IRQ0.
- `IRET` returns to the interrupted execution path.
- final PIC state is `IRR=00h`, `ISR=00h`, `INTR=0`.
- CPU reaches SUCCESS and never reaches FAIL.
- Gate 9R, Gate 10, Gate 11 PIC core, and Gate 11 physical targets remain build regressions.

## Status

**PIT/PIC CORE VALIDATION PASS — physical V30 validation pending.**

Gate 12 must pass on physical V30 hardware before periodic timer behavior or BIOS timing services are introduced.
