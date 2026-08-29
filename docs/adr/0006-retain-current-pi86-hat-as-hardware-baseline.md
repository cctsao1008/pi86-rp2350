# ADR 0006: Retain the Current Pi86 HAT as the Hardware Baseline

- Status: Accepted
- Date: 2026-08-24
- Supersedes: the future V3.0 hardware-redesign requirement in ADR 0003
- Amended by: ADR 0010, which accepts a separately validated CLOCK_STEPPED clock controller

## Context

Earlier architecture work treated a redesigned Pi86 companion board with a
PIO-controllable `READY` path, buffering, and additional safety logic as the
likely long-term hardware direction. That proposal was reasonable while the
project was still evaluating whether the existing Pi86 HAT imposed an
architectural blocker.

Subsequent physical validation established a useful deterministic path on the
existing 2021 Pi86 V20/V30 HAT:

- PIO/DMA can own current-cycle timing without an M33 round trip;
- descriptor-fed native ROM execution is physically validated;
- bounded RAM, interrupt, runtime, and dual-core isolation experiments have
  been demonstrated on the present hardware;
- unsupported or late current-cycle responses can be treated as observable
  high-Z misses rather than repaired through an unvalidated wait-state path.

The project objective is now a programmable, observable, AI-operable companion
chipset around a physical NEC V30. A hardware redesign is not required to
pursue that objective.

## Decision

The existing Pi86 V20/V30 HAT is the project's primary physical hardware
baseline.

No replacement HAT or V3.0 companion board is currently planned.

The project will continue to develop around the constraints of the installed
hardware, including `READY` fixed high. Current-cycle services must therefore
follow the deterministic deadline policy retained from ADR 0003:

1. **Deterministic on-chip hit** — the response is guaranteed before the fixed
   deadline.
2. **Observable unsupported/high-Z miss** — no stale or speculative data is
   driven.
3. **Asynchronous backing/service work** — PSRAM, storage, USB, host tools, and
   other slow work remain outside the active physical cycle.

Clock stopping/stretching on the installed standard D70116C-8 is not promoted
as a vendor-guaranteed timing mechanism for FREE_RUNNING-mode misses. ADR 0010
adds a separate CLOCK_STEPPED clock controller based on physical validation: it issues complete
pulses and pauses only at `CLK=LOW` between pulses. That capability is accepted
for this project hardware baseline without claiming operation inside the
processor vendor's published AC envelope.

A future hardware redesign may be reconsidered only if a demonstrated and
important architectural limitation cannot be addressed within the existing
baseline. Such a decision requires a new ADR; it is not an implicit roadmap
item.

## Relationship to ADR 0003

ADR 0003 remains authoritative for these conclusions:

- the installed standard D70116C-8 is not treated as a fully static CPU;
- the existing HAT cannot insert a normal V30 READY wait state;
- current-cycle M33 lookup is not the default no-wait responder;
- supported hits require deterministic bounded on-chip response;
- unsupported or late cycles remain high-Z and observable;
- PSRAM is backing/refill state unless a separately validated deterministic
  mechanism is introduced.

ADR 0006 supersedes only ADR 0003's requirement that a future V3.0 HAT shall
provide controllable `READY` and related statements that treat that redesign as
an expected project destination.

The historical V3.0 design material remains useful as an engineering study, but
it is not the current implementation roadmap.

## Consequences

Positive consequences:

- software, validation, and tooling can converge on one known physical target;
- firmware work focuses on deterministic response classes and observability
  rather than speculative board redesign;
- hardware evidence remains directly comparable across experiments;
- PSRAM and host services are designed around explicit staging rather than an
  assumed future wait-state mechanism.

Costs and limitations:

- current-cycle cache misses cannot be transparently recovered through READY;
- supported deterministic working sets may remain bounded;
- some general PC-style memory or peripheral behaviors may require staging,
  reduced scope, or remain unsupported on this hardware;
- a future requirement for true wait-state insertion would require revisiting
  the hardware decision explicitly.

## Related work

- Issue #29 — deterministic memory deadlines on the current Pi86 HAT
- Issue #46 — general address-indexed SRAM ROM service
- Issue #51 — canonical firmware/runtime consolidation
- Issue #48 — HAT redesign, closed as not planned
- [`../architecture.md`](../architecture.md)
- [`0003-define-physical-timing-boundary.md`](0003-define-physical-timing-boundary.md)
