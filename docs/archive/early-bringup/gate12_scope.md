# Gate 12 Scope Decision

> **Archive status: SUPERSEDED OR COMPLETED.** This document is retained for
> engineering provenance and is not current architecture or an active plan. See
> the [documentation archive index](../README.md) for its replacement authority.

Gate 12 is a completed functional milestone after Gate 11 PASS.

It introduces only a minimal programmable PIT-compatible channel 0 sufficient to generate one deterministic IRQ0 through the already validated `pi86_pic` path and physical V30 interrupt-entry machinery.

The one-shot timer-to-IRQ dependency has passed on physical V30 hardware. Full periodic BIOS timer behavior remains deferred until the continuous-clock companion-chip front end provides address-qualified memory and I/O service.

The active development boundary is PC1-C address-qualified ROM execution. See [`pc1c_rom_execution_plan.md`](../completed-plans/pc1c_rom_execution_plan.md).

Canonical planning documents:

- `docs/bringup_gate12.md`
- `docs/validation/gate12_pit_irq0_validation.md`
