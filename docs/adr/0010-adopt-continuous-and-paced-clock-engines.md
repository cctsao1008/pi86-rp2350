# ADR 0010: Adopt CONTINUOUS and PACED Clock Engines

- Status: Accepted
- Date: 2026-08-28
- Supersedes: ADR 0003's rejection of clock pacing
- Amends: ADR 0006's fixed-READY timing policy

## Context

The unmodified Pi86 HAT keeps processor `READY` asserted. Earlier work therefore
required every supported bus response to be a prepared deterministic hit while a
clock ran continuously. That policy enabled the accepted 1 MHz PIO/DMA runtime but
left general address-indexed RAM behind a physical integration gate.

The original Raspberry Pi Pi86 design suggested another policy: issue processor
clock pulses under software control. A new RP2350 PACED engine was implemented and
tested with a flat native workload backed by 256 KiB of Internal SRAM.

The physical test completed 218 serviced bus cycles, including reset fetch, taken
branches, `LOOP`, `PUSH`/`POP`, 183 memory reads, 33 memory writes, and two I/O
writes. Native result `0037h`, exit `600Dh`, and all electrical checks passed.

## Decision

The runtime has two execution-engine policies:

1. **CONTINUOUS** keeps a measured clock running and uses PIO/DMA plus prepared
   state to meet every active-cycle deadline.
2. **PACED** issues complete clock pulses and may leave `CLK` low between pulses
   while M33 services general memory or I/O.

PACED never changes mode in the middle of a pulse or active electrical phase. Host
software, USB, filesystems, and storage controllers do not directly own such a
phase.

A running workload may request an engine change through a control I/O port, then
execute `STI; HLT`. RP2350 completes the current pulse, reaches `CLK=LOW`, changes
engine, prepares the destination state, and wakes the processor through the
existing INTR/two-cycle-INTA/ISR/IRET path. Launch metadata may select the initial
engine.

The independent engines are implemented. The cooperative switch handshake remains
to be integrated and physically validated.

## Evidence boundary

PACED operation is an empirical capability of this project hardware and installed
processor samples. Standard Intel 8086 and NEC V30 parts are not represented here
as vendor-guaranteed fully static processors. Each supported clock policy must
retain physical evidence and return the bus to `RESET=HIGH`, `CLK=LOW`, and AD
high-Z on completion or fault.

## Consequences

Positive:

- general Internal-SRAM execution no longer requires a replacement HAT;
- CONTINUOUS performance and PACED flexibility can coexist;
- slow services can be implemented without fabricating READY control;
- workloads choose speed versus service flexibility explicitly.

Costs:

- PACED throughput is lower and timing is intentionally nonuniform;
- engine switching requires a cooperative ABI and explicit safe point;
- interrupt, timeout, and recovery behavior must be validated in both modes;
- External PSRAM still needs separate physical measurement.

## Related documents

- [`../architecture.md`](../architecture.md)
- [`../memory_architecture.md`](../memory_architecture.md)
- [`../validation/paced_internal_sram_general_execution_validation.md`](../validation/paced_internal_sram_general_execution_validation.md)
- [`0003-require-ready-or-deterministic-hits-for-general-memory.md`](0003-require-ready-or-deterministic-hits-for-general-memory.md)
- [`0006-retain-current-pi86-hat-as-hardware-baseline.md`](0006-retain-current-pi86-hat-as-hardware-baseline.md)
