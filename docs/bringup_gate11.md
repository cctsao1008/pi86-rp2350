# Gate 11 - Multi IRQ Priority and Masking Validation

## Objective

Validate multiple interrupt sources using the programmable `pi86_pic` backend.

## Scope

Included:

- IRQ0 and IRQ1
- IMR masking
- IRR pending behavior
- ISR blocking behavior
- Fixed priority arbitration
- EOI recovery

Excluded:

- PIT
- Cascade
- Priority rotation
- Advanced 8259A modes

## Planned Scenario

Initial configuration:

```text
IRQ0 enabled
IRQ1 enabled
```

Test sequence:

```text
Raise IRQ1
Raise IRQ0

Expected:
IRQ0 is serviced first

EOI IRQ0

Expected:
IRQ1 becomes serviceable
```

## Acceptance Criteria

```text
IRQ priority       PASS
IRR handling       PASS
ISR handling       PASS
EOI recovery       PASS
```

Gate 11 extends the validated single IRQ0 path from Gate 10 into multi-source interrupt behavior.
