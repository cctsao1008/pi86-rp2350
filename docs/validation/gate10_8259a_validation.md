# Gate 10 8259A-Compatible PIC Validation Report

- Firmware target: `gate10_8259a`
- Clock model: host-stepped PIO bus service; not an MHz characterization

## Objective

Validate the RP2350 `pi86_pic` implementation as an Intel 8259A-compatible programmable interrupt controller subset using a physical NEC V30 CPU.

## Architecture

```text
NEC V30 CPU
    |
    | x86 bus cycles
    |
    v
RP2350 firmware
    |
    v
pi86_pic behavioral model
```

## Validation Scope

Included:

- ICW1
- ICW2
- ICW3
- ICW4
- IMR
- IRR
- ISR
- Fixed priority arbitration
- IRQ0 request handling
- Two INTA cycles
- Vector delivery on INTA #2
- Non-specific EOI

Excluded:

- PIT
- Cascade mode
- OCW advanced modes
- Priority rotation
- Special 8259A modes

## Result

```text
GATE 10 8259A RESULT: PASS
```

Evidence:

```text
Bus cycles             = 54/320 max
Vector                 = 20h
ISR execution          = PASS
ISR marker [0300]      = 5A
EOI                    = PASS
Final ISR register     = 00h
SUCCESS loop           = 3/3
FAIL loop              = NOT OBSERVED
```

## Significance

Gate 10 demonstrates that RP2350 can provide CPU-visible 8259A-compatible interrupt controller behavior for a real V30 processor.

The implementation is a behavioral compatibility model rather than a transistor-level replica of Intel 8259A.
