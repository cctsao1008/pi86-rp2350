# Gate 11 — Multi IRQ Priority and Masking Validation

## Objective

Validate multiple interrupt source arbitration using the reusable `pi86_pic` backend on the physical NEC V30.

## Scope

Included:

- IRQ0
- IRQ1
- IMR masking
- IRR pending state
- ISR in-service state
- fixed-priority arbitration
- ISR priority blocking
- two INTA cycles per interrupt
- non-specific EOI recovery
- V30 IVT lookup
- ISR execution
- `IRET` return path

Excluded:

- PIT
- cascade mode
- OCW2 advanced rotation modes
- OCW3 diagnostic modes
- special mask / fully nested modes

## Priority Model

8259A-compatible fixed priority:

```text
IRQ0 > IRQ1 > IRQ2 > ... > IRQ7
```

## Validation Scenario

1. Initialize PIC with vector base `20h`.
2. Program `IMR=FCh` to enable IRQ0 and IRQ1.
3. Raise IRQ1 first.
4. Raise IRQ0 while IRQ1 is pending.
5. Verify IRQ0 is selected first despite arriving second.
6. Complete INTA #1 / INTA #2 and deliver vector `20h`.
7. Verify IRQ0 moves to ISR while IRQ1 remains pending in IRR and is blocked.
8. V30 resolves IVT vector `20h`, executes ISR0 at `F000:0100`, writes marker `A0h`, issues non-specific EOI, and returns with `IRET`.
9. Verify EOI makes pending IRQ1 serviceable.
10. Complete the second INTA #1 / INTA #2 pair and deliver vector `21h`.
11. V30 resolves IVT vector `21h`, executes ISR1 at `F000:0120`, writes marker `A1h`, issues EOI, and returns with `IRET`.
12. Verify final controller state is idle.

## Physical Hardware Result

```text
Serviced bus cycles                  = 89/480 max
First reset-vector WORD read         = PASS
ICW1 / ICW2 / ICW3 / ICW4           = YES / YES / YES / YES
PIC initialized / vector base        = YES / 20h
IMR programmed                       = FCh (expected FCh)
IRQ1 then IRQ0 raised / IRR=03h      = YES / YES
INTAK cycles                         = 4 total (2 first / 2 second)
IRQ0 selected first / vector 20h     = YES / YES
IRQ1 blocked while IRQ0 in service   = YES
IRQ0 IVT offset/segment              = YES / YES
IRQ0 ISR fetch / marker A0h          = YES / YES
IRQ0 EOI / IRQ1 becomes serviceable = YES / YES
IRQ1 selected second / vector 21h    = YES / YES
IRQ1 IVT offset/segment              = YES / YES
IRQ1 ISR fetch / marker A1h          = YES / YES
IRQ1 EOI                             = YES
Stack frame writes x2 7FFA/7FFC/7FFE= 2 / 2 / 2
Final IRR / ISR / INTR               = 00h / 00h / 0
Success-loop hits F002E              = 3/3 required
GATE 11 PHYSICAL V30 RESULT          = PASS
```

The CPU was then halted with `RESET=HIGH`, `CLK=LOW`, and the AD bus high-Z.

## Acceptance Criteria

- higher-priority IRQ wins arbitration — PASS
- correct IRR -> ISR state transition — PASS
- INTA #2 supplies the expected vector — PASS
- lower-priority pending IRQ is blocked while higher-priority IRQ is in service — PASS
- EOI clears the in-service state — PASS
- lower-priority pending IRQ becomes serviceable after EOI — PASS
- both IVT entries are resolved by the physical V30 — PASS
- both ISRs execute and write the expected markers — PASS
- both interrupt paths return through `IRET` — PASS
- final `IRR=00h`, `ISR=00h`, `INTR=0` — PASS
- success loop observed three times — PASS

## Evidence

- Hardware test target: `gate11_irq_priority`
- Controller-only preflight target: `gate11_pic_priority`
- GitHub Issue #41: Gate 11 — Multi IRQ Priority and Masking Validation
- Raw USB CDC / bus trace archive: Google Drive `02_Bringup_Logs/Gate11_MultiIRQ_Priority/pi86-rp2350_Gate11_USB_CDC_Raw_Log`

## Status

**PASS — physical V30 validation complete.**
