# pi86-rp2350 Gate 11 Milestone

## Result

**Gate 11 — Multi IRQ Priority and Masking Validation: PASS**

Physical NEC V30 validation completed successfully on the Waveshare RP2350-PiZero + original Pi86 V20/V30 HAT baseline.

## Proven Behavior

- PIC initialized through ICW1-4 with vector base `20h`.
- `IMR=FCh` enabled IRQ0 and IRQ1.
- IRQ1 was raised first and IRQ0 second; fixed priority selected IRQ0 first.
- IRQ0 completed two physical INTA cycles and received vector `20h`.
- IRQ1 remained pending and was blocked while IRQ0 was in service.
- IRQ0 ISR executed at `F000:0100`, wrote marker `A0h`, sent non-specific EOI, and returned through `IRET`.
- Pending IRQ1 became serviceable after IRQ0 EOI.
- IRQ1 completed two physical INTA cycles and received vector `21h`.
- IRQ1 ISR executed at `F000:0120`, wrote marker `A1h`, sent EOI, and returned through `IRET`.
- Two interrupt stack frames were observed at `7FFA/7FFC/7FFE`.
- Final controller state was `IRR=00h`, `ISR=00h`, `INTR=0`.
- Success loop `F002E` was observed 3/3 times.
- CPU was safely halted with `RESET=HIGH`, `CLK=LOW`, and AD bus high-Z.

## Evidence

- `tests/gate11_pic_priority` — controller-only fixed-priority preflight PASS.
- `tests/gate11_irq_priority` — physical V30 validation PASS.
- `docs/validation/gate11_multi_irq_priority_validation.md`.
- GitHub Issue #41 — closed completed.
- Google Drive: `02_Bringup_Logs/Gate11_MultiIRQ_Priority/pi86-rp2350_Gate11_USB_CDC_Raw_Log`.

## Next Boundary

Gate 12 introduces a minimal programmable PIT-compatible channel 0 that raises IRQ0 only through the validated `pi86_pic` path.

Proposed immutable milestone tag: `pi86-rp2350-gate11-pass`.
