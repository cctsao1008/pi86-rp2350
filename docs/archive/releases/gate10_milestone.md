# pi86-rp2350 Gate 10 Milestone

## Status

Gate 10 — 8259A-compatible programmable PIC validation: **PASS**

## Validated Scope

- ICW1 / ICW2 / ICW3 / ICW4 initialization
- IMR programming
- IRQ0 request handling
- IRR to ISR transition
- Fixed priority arbitration
- Two INTA cycles
- Vector delivery on INTA #2
- V30 interrupt entry and ISR execution
- Non-specific EOI

## Physical V30 Evidence

Validated on real NEC V30 hardware with RP2350 as the chipset backend.

Key results:

- Reset vector fetch: PASS
- PIC initialization: PASS
- Vector base: 20h
- IRQ0 service: PASS
- ISR execution: PASS
- ISR marker: 0x5A
- EOI recovery: PASS
- Success loop: 3/3
- Fail loop: not observed

## Next Milestone

Gate 11 — Multi IRQ priority and masking validation

Planned scope:

- Multiple IRQ sources
- IMR masking behavior
- IRR pending behavior
- ISR blocking behavior
- Fixed priority arbitration
- EOI recovery

Excluded:

- PIT
- Cascade mode
- Priority rotation
- Advanced 8259A modes
