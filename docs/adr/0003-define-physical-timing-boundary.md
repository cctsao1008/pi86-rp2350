# ADR 0003: Define the Physical Timing Boundary

- Status: Accepted
- Date: 2026-08-19
- Updated: 2026-08-29

## Context

The current Pi86 HAT keeps the physical processor `READY` input asserted. The
RP2350 therefore cannot insert an ordinary processor wait state for a late
memory, I/O, filesystem, USB, Host, or external-storage response.

Intel 8086 and standard NEC V30 parts also do not provide one shared,
vendor-guaranteed static-clock contract. CLOCK_STEPPED operation is an accepted
project capability for physically validated workloads, not a general promise
that an active bus cycle may be paused at any arbitrary phase.

## Decision

Current-cycle processor-bus behavior must be supplied by prepared RP2350 state
through PIO/DMA or another physically validated bounded path.

Every processor-visible service follows one of these policies:

1. **Prepared hit:** the response is selected and driven before the fixed bus
   deadline.
2. **Explicitly unsupported cycle:** AD remains high-Z and the runtime reports
   the miss or fault.
3. **Clock-stepped workload:** the complete clock-step behavior and supported
   cycle types are part of that workload's accepted physical contract.

M33 software, Host software, USB, filesystems, NOR flash, SD, and an arbitrary
external-PSRAM lookup do not answer a current bus cycle on demand.

External PSRAM is a capacity/backing tier. A PSRAM-backed execution path must
stage or cache processor-visible state inside a bounded response mechanism and
must define its miss behavior before it can be called physically supported.

## Consequences

- realtime bus response remains separated from general Host/runtime software;
- Internal SRAM is the first practical processor-visible backing tier;
- prepared FREE_RUNNING and validated CLOCK_STEPPED operation may coexist;
- unsupported cycles fail observably instead of returning invented data;
- adding a future controllable READY path would expand the implementation
  options but is not required by the current project scope.

## Related documents

- [`0006-retain-current-pi86-hat-as-hardware-baseline.md`](0006-retain-current-pi86-hat-as-hardware-baseline.md)
- [`0010-adopt-free-running-and-clock-stepped-execution.md`](0010-adopt-free-running-and-clock-stepped-execution.md)
- [`../architecture.md`](../architecture.md)
- [`../hardware.md`](../hardware.md)
- [`../memory_architecture.md`](../memory_architecture.md)
