# ADR 0011: Constrain Intel 8086 Execution to a Continuous In-Spec Clock

- Status: Accepted
- Date: 2026-09-14
- Supersedes: the Intel 8086 `CLOCK_STEPPED` general-execution claim in ADR 0010
- Preserves: ADR 0003 fixed-`READY` timing boundary
- Does not decide: NEC V30 clock-stop behavior

## Context

The current Pi86 HAT ties the processor `READY` input asserted. RP2350 therefore cannot insert ordinary Intel 8086 wait states on an active bus cycle.

ADR 0010 introduced two execution modes:

1. `FREE_RUNNING`, where the processor clock runs continuously and realtime PIO/DMA/prepared state must satisfy each active bus cycle; and
2. `CLOCK_STEPPED`, where RP2350 emits complete clock pulses and may hold `CLK` low between pulses while M33 software services general memory or I/O.

`CLOCK_STEPPED` was physically demonstrated as an empirical project capability. Subsequent Intel clock-contract review for Issue #102 established that the installed Intel P8086-2 is not specified for arbitrary stopped-clock operation. Intel documentation requires a minimum processor clock of 2 MHz because internal state uses dynamic storage and explicitly warns that single-step/single-cycle operation cannot be implemented by simply stopping the clock.

The repository also contains accepted physical Intel observations at 1 MHz `FREE_RUNNING`. Those measurements remain valid evidence of what the tested sample completed, but 1 MHz is below the Intel-documented 2 MHz minimum and therefore is not a vendor-compliant operating point.

The FreeRTOS #75 investigation makes this distinction operationally important. Production FreeRTOS machine code, caller state, task restore, ISR/IRET behavior, and the RP2350 executor/controller transaction model have all passed their current software-side checks. The remaining uncertainty is at the physical processor/bus timing boundary.

## Decision

For Intel 8086 execution, the project adopts the following canonical contract:

> **The Intel 8086 clock must run continuously at or above the documented minimum frequency during general execution.**

On the current unmodified Pi86 HAT:

1. `CLOCK_STEPPED` is not a compliant Intel 8086 general-execution mode because it can hold `CLK` low for an unbounded software-controlled interval.
2. `FREE_RUNNING` is the architectural basis for compliant Intel execution, but the operating point must be at or above the Intel minimum before it is described as compliant.
3. Because `READY` is fixed asserted, every supported active memory, I/O, and interrupt-acknowledge cycle must be satisfied by a bounded realtime response path.
4. M33 foreground code, USB, Host software, filesystems, storage lookup, and any other unbounded service are outside the active-cycle timing contract.
5. A bus transaction that cannot be completed within the bounded realtime path must be rejected or faulted observably; it must not be completed by stopping the Intel clock indefinitely.
6. General variable-latency processor bus service requires a future hardware or architecture change such as controllable `READY`, external wait-state logic, or a fully realtime hardware/PIO/DMA responder.

This ADR does not define the NEC V30 clock-stop/minimum-frequency contract. That target requires its own vendor-document audit.

## Consequences

Positive:

- Intel 8086 architectural claims now match the processor vendor operating contract;
- the fixed-`READY` hardware boundary and processor clock contract are no longer contradictory;
- physical FreeRTOS debugging can distinguish software correctness from out-of-spec silicon operation;
- future Intel timing work has a clear acceptance target: continuous clock plus bounded realtime bus service.

Costs:

- existing `CLOCK_STEPPED` Intel results become empirical/out-of-spec evidence rather than a canonical compliant execution mode;
- existing 1 MHz Intel `FREE_RUNNING` results remain useful measurements but are also out of specification;
- processor-visible services that depend on M33 software between cycles must be redesigned, staged into a deterministic fast path, or declared unsupported for compliant Intel execution;
- physical #75 comparison should not be treated as conclusive until executed under a compliant Intel clock contract.

## Historical evidence policy

Previously accepted physical results are not erased or rewritten. They remain factual records of the exact test conditions under which they were obtained.

They must be described precisely:

```text
physical PASS under measured out-of-spec clocking
    = accepted empirical observation
    != Intel-guaranteed operating contract
```

This preserves the value of bring-up evidence without turning sample tolerance into an architectural guarantee.

## Relationship to ADR 0010

ADR 0010 remains the historical decision that introduced `FREE_RUNNING` and `CLOCK_STEPPED` as project execution modes.

ADR 0011 narrows that decision for the Intel 8086 target:

```text
Intel 8086
  FREE_RUNNING   -> canonical execution model, only compliant when clock is in spec
  CLOCK_STEPPED  -> empirical diagnostic/experimental mode, not compliant general execution

NEC V30
  -> unchanged by this ADR; separate clock-contract audit required
```

## Required follow-up

1. Define the minimum compliant Intel `FREE_RUNNING` frequency and timing budget.
2. Audit every currently supported processor-visible memory, I/O, INTA, and interrupt path against that continuous-clock deadline.
3. Identify which services already fit a prepared PIO/DMA/realtime path and which still depend on M33 software latency.
4. Add an in-spec physical Intel validation point before re-running the FreeRTOS #75 comparison.
5. Keep controllable `READY` or equivalent wait-state hardware as the explicit future option for general variable-latency service.
6. Audit NEC V30 clock requirements independently.

## Related documents

- [`../architecture/intel_8086_clock_contract.md`](../architecture/intel_8086_clock_contract.md)
- [`0003-define-physical-timing-boundary.md`](0003-define-physical-timing-boundary.md)
- [`0006-retain-current-pi86-hat-as-hardware-baseline.md`](0006-retain-current-pi86-hat-as-hardware-baseline.md)
- [`0010-adopt-free-running-and-clock-stepped-execution.md`](0010-adopt-free-running-and-clock-stepped-execution.md)
- Issue #75 — FreeRTOS indefinite-block physical stall
- Issue #102 — Intel P8086-2 clock-contract audit
