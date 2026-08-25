# ADR 0009: Extend the Runtime to Intel 8086 and NEC V30

**Status:** Accepted
**Date:** 2026-08-25

## Context

The Host-managed runtime was established and validated with a physical NEC V30.
On 2026-08-25, the V30 was removed and an Intel `P8086-2` was installed in the
same original Pi86 HAT. Without changing the companion-runtime design, the Intel
processor completed 55 sequence-numbered heartbeat exchanges with zero loss and
then completed an interactive command exchange.

The retained Host output still used fixed `V30` presentation strings. Those
strings were not processor identification; neither processor implements CPUID.

## Decision

The canonical physical-processor scope is:

> **Intel 8086 / NEC V30**

The project definition becomes:

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel
> 8086 and NEC V30 processors.**

Processor identity is explicit Host metadata:

```text
--processor intel-8086
--processor nec-v30
```

The runtime does not guess the installed processor. Protocol-version-1 payloads
retain their existing bytes for compatibility; Host display and evidence record
the declared physical processor.

## Consequences

- The repository name remains `pi86-rp2350`.
- Existing NEC V30 validation remains historically exact.
- Intel 8086 support is part of the canonical scope.
- A complete Intel 8086 cold-boot transcript is useful additional evidence, not
  a prerequisite for the scope decision.
- Native workloads intended for both processors must remain within their shared
  Intel 8086 instruction and bus semantics unless a processor-specific workload
  is explicitly selected.

## Relationship to ADR 0008

ADR 0009 extends ADR 0008's physical-processor scope. It does not change the
three-role architecture, resource ownership, Host runtime, or timing boundary.
