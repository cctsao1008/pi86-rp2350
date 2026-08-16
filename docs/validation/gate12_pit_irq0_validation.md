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

## Planned Evidence

Capture and report at minimum:

- PIT control-word write at port `43h`
- PIT channel-0 count write(s) at port `40h`
- programmed count value
- PIT terminal-count event
- PIC `IRR`, `ISR`, `IMR`, and `INTR` around IRQ0 delivery
- two INTA cycles
- vector `20h` on INTA #2
- IVT reads at `00080h` / `00082h`
- interrupt stack frame writes
- ISR fetch
- marker write
- non-specific EOI
- `IRET` return
- final PIC idle state
- SUCCESS / FAIL loop observations

## PASS Boundary

Controller-only PIT state tests are useful preflight evidence but do not close Gate 12. Gate 12 closes only after the timer-programming and IRQ0 path completes on the physical V30.

## Status

**PLANNED — not yet PASS.**
