# Gate 11 — Multi IRQ Priority and Masking Validation

## Objective

Validate multiple interrupt source arbitration using the reusable `pi86_pic` backend.

## Scope

Included:

- IRQ0
- IRQ1
- IMR masking
- IRR pending state
- ISR in-service state
- Fixed priority arbitration
- Two INTA cycles
- Non-specific EOI recovery

Excluded:

- PIT
- Cascade mode
- OCW2 advanced rotation modes
- OCW3 diagnostic modes

## Expected Priority Model

8259A-compatible fixed priority:

```
IRQ0 > IRQ1 > IRQ2 ... > IRQ7
```

## Validation Scenario

1. Initialize PIC with vector base `20h`.
2. Enable IRQ0 and IRQ1.
3. Raise IRQ1.
4. Raise IRQ0 while IRQ1 is pending.
5. Verify IRQ0 is serviced first.
6. Complete INTA #1 / INTA #2 sequence.
7. Execute ISR and issue EOI.
8. Verify IRQ1 becomes serviceable.
9. Verify vector `21h` is delivered.

## Acceptance Criteria

- Higher priority IRQ wins arbitration.
- IRR to ISR transition is correct.
- INTA #2 returns the expected vector.
- EOI clears the in-service state.
- Lower priority pending IRQ can be serviced after recovery.

## Status

Planning stage after Gate 10 8259A-compatible PIC validation PASS.
