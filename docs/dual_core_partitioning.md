# RP2350 Dual-Core Workload Partitioning

## Scope

This document defines the logical ownership boundary between the RP2350 deterministic bus plane, the realtime M33 role, and the asynchronous service M33 role.

The architecture is **role-based**, not permanently tied to Core 0 or Core 1. A numbered core assignment is an implementation choice that may change with measured IRQ behavior, SDK constraints, SRAM contention, and service load.

## Governing rule

> **PIO/DMA own the current V30 bus cycle. The realtime M33 role prepares and supervises future CPU-visible state. The service M33 role owns asynchronous system complexity.**

```text
Physical V30
     |
PIO / DMA bus plane
     |
realtime M33 role
     |
bounded ownership transfer
     |
service M33 role
```

The current Pi86 HAT holds V30 `READY` high, so a no-wait transaction cannot depend on an M33 lookup, an inter-core round trip, USB, PSRAM, Flash, or other unbounded service work.

## Deterministic bus plane

PIO and DMA own operations that must complete inside the current physical V30 cycle, including:

- clock and phase generation;
- address/control capture and cycle qualification;
- AD0-AD15 input/output timing and `PINDIRS` ownership;
- prestaged ROM/RAM responses;
- deterministic write capture/replay paths;
- prestaged interrupt-acknowledge response;
- raw passive trace transport.

No current-cycle path may depend on `printf`, USB callbacks, a filesystem, dynamic allocation, an inter-core reply, or an unbounded lock.

## Realtime M33 role

The realtime role prepares and supervises state that may affect later V30-visible behavior:

- configure and arm PIO/DMA engines;
- build response keys, descriptors, and hot-memory state before use;
- maintain address maps and CPU-visible device state;
- supervise deterministic refill or publication outside the active cycle;
- maintain interrupt/timer state and prestage future responses;
- own reset and terminal-safe policy;
- detect starvation, stale descriptors, deadline faults, and illegal ownership;
- publish compact counters and raw events without human-readable formatting.

The realtime role must not synchronously wait for the service role while the V30 is running.

## Service M33 role

The service role owns work whose latency is not part of the V30 bus contract:

- USB and host command handling;
- trace decode, formatting, and persistence;
- ROM/test-image management;
- PSRAM and other bulk transfers;
- disk-image or compatibility-profile backend work;
- diagnostics, configuration, and management interfaces.

The service role may prepare buffers or request state changes, but it is never a synchronous dependency of a no-wait V30 transaction.

## Inter-core contract

Communication between roles uses bounded ownership transfer rather than shared mutable behavior in the critical path.

Preferred mechanisms are fixed-size single-producer/single-consumer rings and explicit publication at safe boundaries.

Required properties:

- queue capacity and element layout are bounded;
- publication order is explicit and memory-ordered;
- queue-full behavior is non-blocking and observable;
- trace/log overflow drops records rather than stalling the V30;
- mutable device objects are not transferred without an ownership protocol;
- reset and bus ownership have a single authority;
- inter-core FIFO is reserved for lightweight signaling rather than bulk transport;
- neither role depends on an unbounded lock held by the other.

## Failure isolation

Asynchronous service faults must not silently change bus timing or ownership. Examples include:

- USB disconnect or backpressure;
- service-role stall;
- a full trace ring;
- slow storage or image access;
- malformed host commands.

The realtime role may report, recover, or restart services later, but the deterministic bus plane must remain electrically safe and timing-stable.

## Core-number assignment

The architecture intentionally avoids treating `Core 0` and `Core 1` as permanent semantic identities.

A concrete placement may be selected based on:

- PIO/DMA IRQ routing and latency;
- USB SDK and alarm-pool constraints;
- shared SRAM-bank contention;
- DMA versus instruction/data traffic;
- Flash/XIP stalls;
- queue publication latency;
- V30 trace and response integrity under service load.

Code and canonical documentation should therefore prefer role names such as `realtime_core` and `service_core` unless a specific validation target requires a numbered mapping.

## Existing physical evidence

Historical dual-core validation remains useful evidence for this architecture:

- **DC-A** established bounded inter-core rings, non-blocking overflow accounting, service-role stall isolation, and service recovery around an unchanged V30 regression.
- **DC-B0** established that host/CDC reporting can be delayed until after the V30 run without making USB a dependency of V30 execution.
- **DC-B1-A** established bounded replay of authentic captured trace data, ordered drain, counted drops under service stall, and recovery after resume.

These records describe specific tested implementations and may include explicit Core 0/Core 1 assignments required by the Pico SDK or the validation target. Those assignments are historical implementation details, not the architectural contract.

See:

- [`validation/dc_a_dual_core_foundation_validation.md`](validation/dc_a_dual_core_foundation_validation.md)
- [`validation/dc_b0_service_core_output_validation.md`](validation/dc_b0_service_core_output_validation.md)
- [`validation/dc_b1a_trace_backpressure_validation.md`](validation/dc_b1a_trace_backpressure_validation.md)

## Relationship to the host/AI interface

The host-side Observe / Control / Experiment interface sits above this partitioning model.

Host tools and AI agents may consume structured state, request bounded operations, and analyze experiments, but they remain outside current-cycle V30 timing. The service role terminates host-facing complexity; the realtime role only accepts state changes through explicit bounded publication.

See [`architecture.md`](architecture.md), [`ai_bridge_architecture.md`](ai_bridge_architecture.md), and [`companion_service_abi.md`](companion_service_abi.md).
