# Gate 12 — Minimal Programmable PIT IRQ0 Validation

> **Archive status: SUPERSEDED OR COMPLETED.** This document is retained for
> engineering provenance and is not current architecture or an active plan. See
> the [documentation archive index](../README.md) for its replacement authority.

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

## Scope

Included:

- PIT control port `43h`
- PIT channel 0 data port `40h`
- channel 0 only
- LSB/MSB count programming
- binary Mode 0 one-shot terminal-count behavior
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
- advanced latch/read behavior
- periodic BIOS tick compatibility
- BIOS time-of-day services
- timer calibration / final clock-rate optimization

## Core Validation

Target: `gate12_pit_core`

**PASS.**

Validated controller-state sequence:

```text
PIT control 43h = 30h
PIT channel 0 reload = 0004h
no terminal-count event before final tick
terminal count produces exactly one event
terminal count -> pi86_pic IRQ0
PIC IRR = 01h
PIC INTR asserted
IRQ0 two-cycle acknowledge -> vector 20h
PIC ISR = 01h
non-specific EOI
final PIC ISR = 00h, INTR deasserted
```

## Physical V30 Validation

Target: `gate12_pit_irq0`

**PASS.**

The physical V30 executed the actual programming sequence:

```text
PIC: ICW1-4, IMR=FEh
PIT: OUT 43h,30h
     OUT 40h,08h
     OUT 40h,00h
```

The RP2350 PIT backend then reached terminal count and raised IRQ0 only through `pi86_pic`.

Hardware result:

```text
Serviced bus cycles                  = 76/360 max
First reset-vector WORD read         = PASS
PIC ICW1 / ICW2 / ICW3 / ICW4       = YES / YES / YES / YES
PIC IMR / vector base                = FEh / 20h
PIT OUT 43h control 30h              = YES
PIT OUT 40h LSB/MSB                  = YES / YES
PIT programmed / reload              = YES / 0008h
No IRQ0 before PIT terminal count    = YES
PIT terminal count / IRQ0 routed     = YES / YES
INTA cycles                          = 2 total (1 first / 1 second)
IRQ0 selected / vector 20h           = YES / YES
IRQ0 IVT offset/segment              = YES / YES
IRQ0 ISR fetch / marker A0h          = YES / YES
IRQ0 EOI                             = YES
Stack writes 7FFA/7FFC/7FFE          = 1 / 1 / 1
IRET stack reads 7FFA/7FFC/7FFE      = 1 / 1 / 1
Final IRR / ISR / INTR               = 00h / 00h / 0
Success-loop hits F0033              = 3/3 required
GATE 12 PHYSICAL V30 RESULT          = PASS
```

## Architectural Rule

The PIT must not bypass the interrupt controller.

Validated path:

```text
PIT event
  -> pi86_pic_raise_irq(&pic, 0)
  -> pi86_pic_intr_asserted(&pic)
  -> V30 INTR pin
```

This preserves the Gate 10/11 PIC contract as a regression invariant.

## Acceptance Result

All Gate 12 acceptance criteria are satisfied:

- physical V30 PIT writes on `43h` / `40h` observed;
- channel-0 programming accepted;
- no IRQ0 before terminal count;
- exactly one IRQ0 generated for the test;
- PIC IRR/INTR transition observed;
- exactly two physical INTA cycles;
- vector `20h` supplied on INTA #2;
- IRQ0 moved IRR -> ISR;
- IVT and ISR execution observed;
- marker `A0h` written;
- EOI cleared ISR;
- `IRET` restored the interrupted state;
- final `IRR=00h`, `ISR=00h`, `INTR=0`;
- SUCCESS observed 3/3;
- CPU shut down in RESET with CLK low and AD high-Z.

## Status

**PASS — physical NEC V30 validation complete.**

Gate 12 is closed. Periodic BIOS timer behavior remains deferred. The next active boundary is Performance Characterization 1: establish the maximum sustainable physical V30 clock of the current RP2350 bus architecture before deeper BIOS/DOS expansion.
