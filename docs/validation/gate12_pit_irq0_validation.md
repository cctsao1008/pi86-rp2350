# Gate 12 — PIT Channel 0 to IRQ0 Physical Validation

## Goal

Validate a minimal programmable PIT-compatible channel 0 that is programmed by the physical V30 and raises IRQ0 only through the validated `pi86_pic` path.

## Required End-to-End Path

```text
OUT 43h / OUT 40h
    -> PIT channel 0 state
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

## Core Validation

Target: `gate12_pit_core`

**PASS.** The PIT/PIC controller-state path validated the minimal channel-0 Mode 0 one-shot behavior and IRQ0 routing through `pi86_pic`.

## Physical V30 Result

Target: `gate12_pit_irq0`

**PASS.**

The physical NEC V30 programmed both PIC and PIT through real I/O cycles and completed the full timer-interrupt path.

Observed hardware evidence:

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

Decisive sequence:

```text
V30 OUT 43h,30h
V30 OUT 40h,08h
V30 OUT 40h,00h
    -> PIT reload 0008h
    -> no IRQ0 before terminal count
    -> terminal count
    -> pi86_pic IRR=01h, IMR=FEh, INTR=1
    -> INTA #1: no vector, IRR=00h, ISR=01h
    -> INTA #2: vector 20h
    -> IVT reads 00080h / 00082h -> F000:0100
    -> V30 saves FLAGS/CS/IP at 7FFE/7FFC/7FFA
    -> ISR F000:0100
    -> marker [0300] = A0h
    -> non-specific EOI at port 20h
    -> ISR=00h, INTR=0
    -> IRET reads 7FFA/7FFC/7FFE
    -> CPU returns to interrupted path
    -> SUCCESS F0033 observed 3/3
```

## Architectural Invariant

The PIT did not bypass the interrupt controller.

Validated path:

```text
PIT terminal count
  -> pi86_pic_raise_irq(&pic, 0)
  -> pi86_pic_intr_asserted(&pic)
  -> V30 INTR
```

The forbidden shortcut `PIT event -> direct v30_bus_set_intr()` was not used as the acceptance path.

## PASS Boundary

Gate 12 requires both:

- `gate12_pit_core` controller-state validation PASS;
- `gate12_pit_irq0` physical NEC V30 end-to-end validation PASS.

Both conditions are satisfied.

## Status

**PASS — Gate 12 closed on physical NEC V30 hardware.**

Periodic BIOS timer behavior remains a separate future compatibility milestone. The next project boundary is Performance Characterization 1: measure the maximum sustainable physical V30 clock of the current RP2350 bus architecture before deeper BIOS/DOS expansion.
