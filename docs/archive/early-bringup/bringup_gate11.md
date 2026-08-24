# Gate 11 - Multi IRQ Priority and Masking Validation

> **Archive status: SUPERSEDED OR COMPLETED.** This document is retained for
> engineering provenance and is not current architecture or an active plan. See
> the [documentation archive index](../README.md) for its replacement authority.

## Objective

Validate multiple interrupt sources using the programmable `pi86_pic` backend on the physical NEC V30.

## Scope

Included:

- IRQ0 and IRQ1
- IMR masking
- IRR pending behavior
- ISR blocking behavior
- fixed-priority arbitration
- two INTA cycles per interrupt
- EOI recovery
- IVT resolution
- ISR execution
- `IRET` return path

Excluded:

- PIT
- cascade
- priority rotation
- advanced 8259A modes

## Validated Scenario

Initial configuration:

```text
vector base = 20h
IMR = FCh
IRQ0 enabled
IRQ1 enabled
```

Hardware sequence:

```text
Raise IRQ1
Raise IRQ0
IRR = 03h

IRQ0 wins fixed-priority arbitration
INTA #1 / #2 -> vector 20h
IRQ0 enters ISR
IRQ1 remains pending and blocked
ISR0 writes A0h
EOI IRQ0
IRET

IRQ1 becomes serviceable
INTA #1 / #2 -> vector 21h
IRQ1 enters ISR
ISR1 writes A1h
EOI IRQ1
IRET

Final IRR=00h ISR=00h INTR=0
```

## Hardware Result

```text
IRQ priority                        PASS
IRR handling                        PASS
ISR handling                        PASS
ISR priority blocking               PASS
EOI recovery                        PASS
IRQ0 vector 20h                     PASS
IRQ1 vector 21h                     PASS
IRQ0 IVT / ISR / marker A0h         PASS
IRQ1 IVT / ISR / marker A1h         PASS
IRET path x2                        PASS
Final controller idle state         PASS
Success-loop F002E                  3/3
GATE 11 PHYSICAL V30 RESULT          PASS
```

Gate 11 extends the validated single IRQ0 path from Gate 10 into physical multi-source fixed-priority interrupt behavior.

## Status

**PASS**

The next dependency boundary is Gate 12: introduce a minimal programmable PIT timer source that raises IRQ0 through the already validated PIC path, without yet expanding into full BIOS timer compatibility.
