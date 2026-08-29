# ADR 0010: Adopt FREE_RUNNING and CLOCK_STEPPED Execution Clock Modes

- Status: Accepted
- Date: 2026-08-28
- Supersedes: ADR 0003 physical timing boundary
- Amends: ADR 0006's fixed-READY timing policy

## Context

The unmodified Pi86 HAT keeps processor `READY` asserted. Earlier work therefore
required every supported bus response to be a prepared deterministic hit while a
clock ran continuously. That policy enabled the accepted 1 MHz PIO/DMA runtime but
left general address-indexed RAM behind a physical integration gate.

The original Raspberry Pi Pi86 design suggested another policy: issue processor
clock pulses under software control. A new RP2350 CLOCK_STEPPED controller was implemented and
tested with a flat native workload backed by 256 KiB of Internal SRAM.

The physical test completed 218 serviced bus cycles, including reset fetch, taken
branches, `LOOP`, `PUSH`/`POP`, 183 memory reads, 33 memory writes, and two I/O
writes. Native result `0037h`, exit `600Dh`, and all electrical checks passed.

## Decision

The runtime has two Execution Clock Modes:

1. **FREE_RUNNING** keeps a measured clock running and uses PIO/DMA plus prepared
   state to meet every active-cycle deadline.
2. **CLOCK_STEPPED** issues complete clock pulses and may leave `CLK` low between pulses
   while M33 services general memory or I/O.

CLOCK_STEPPED never changes mode in the middle of a pulse or active electrical phase. Host
software, USB, filesystems, and storage controllers do not directly own such a
phase.

A running workload may request a clock-mode change through a control I/O port.
RP2350 commits that complete I/O write cycle, reaches `CLK=LOW`, changes clock mode,
and lets the native interrupt handler return under the selected policy. The
foreground may then execute `STI; HLT` and remains wakeable through the existing
INTR/two-cycle-INTA/ISR/IRET path. Launch metadata may select the initial clock mode.

The independent clock controllers and cooperative request/commit handshake are
implemented and physically validated. Canonical Host lifecycle selection remains
to be integrated.

## Evidence boundary

CLOCK_STEPPED operation is an empirical capability of this project hardware and installed
processor samples. Standard Intel 8086 and NEC V30 parts are not represented here
as vendor-guaranteed fully static processors. Each supported clock policy must
retain physical evidence and return the bus to `RESET=HIGH`, `CLK=LOW`, and AD
high-Z on completion or fault.

## Consequences

Positive:

- general Internal-SRAM execution no longer requires a replacement HAT;
- FREE_RUNNING performance and CLOCK_STEPPED flexibility can coexist;
- slow services can be implemented without fabricating READY control;
- workloads choose speed versus service flexibility explicitly.

Costs:

- CLOCK_STEPPED throughput is lower and timing is intentionally nonuniform;
- clock-mode switching requires a cooperative ABI and explicit safe point;
- interrupt, timeout, and recovery behavior must be validated in both modes;
- External PSRAM still needs separate physical measurement.

## Related documents

- [`../architecture.md`](../architecture.md)
- [`../memory_architecture.md`](../memory_architecture.md)
- [`../validation/clock_stepped_internal_sram_general_execution_validation.md`](../validation/clock_stepped_internal_sram_general_execution_validation.md)
- [`../validation/execution_clock_mode_transition_validation.md`](../validation/execution_clock_mode_transition_validation.md)
- [`0003-define-physical-timing-boundary.md`](0003-define-physical-timing-boundary.md)
- [`0006-retain-current-pi86-hat-as-hardware-baseline.md`](0006-retain-current-pi86-hat-as-hardware-baseline.md)
