# Gate 12 Scope Decision

Gate 12 is the next active development boundary after Gate 11 PASS.

It introduces only a minimal programmable PIT-compatible channel 0 sufficient to generate one deterministic IRQ0 through the already validated `pi86_pic` path and physical V30 interrupt-entry machinery.

Full periodic BIOS timer behavior is explicitly deferred until this one-shot timer-to-IRQ dependency has passed on hardware.

Canonical planning documents:

- `docs/bringup_gate12.md`
- `docs/validation/gate12_pit_irq0_validation.md`
