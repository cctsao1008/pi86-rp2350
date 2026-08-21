# RP2350 Dual-Core Workload Partitioning

## Status and scope

- Status: adopted architecture guideline
- Date: 2026-08-21
- Target: physical NEC V30 with RP2350B companion chipset
- DC-A implementation: commit `6e20b4a`, awaiting physical acceptance

This document defines logical ownership between the RP2350 hard-real-time data
plane, the real-time M33 role, and the service M33 role. It does not permanently
assign those roles to numbered cores. Core placement remains a measured design
choice under ADR 0002.

## Governing rule

> PIO/DMA owns the current V30 bus cycle. The real-time core owns CPU-visible
> control policy. The service core owns asynchronous system complexity.

The shorter rule "Core 0 owns CPU-visible time; Core 1 owns complexity" is a
useful deployment shorthand, but it is not yet a physical core-number contract.

```text
Physical V30
     |
     v
PIO / DMA data plane
     |  hard deadline: current bus cycle
     v
real-time M33 role
     |  bounded queues and ownership transfer
     v
service M33 role
```

## Evidence behind the boundary

The current HAT fixes V30 `READY` high. A response cannot wait for an M33
lookup, an inter-core round trip, USB, PSRAM, Flash, or MicroSD.

Physical PC1-C evidence established that:

- an M33 current-cycle SRAM lookup can miss the response deadline even at the
  0.300 MHz baseline;
- PIO-local exact matching and PIO-direct AD/PINDIRS response meet the deadline;
- C0C1-B2-B can capture a live V30 RAM write and replay it later in the same run
  entirely inside PIO1, with no current-cycle M33 work;
- C0C1-B2-C can retain two independent words, preserve low/high byte-lane
  coherence, consume all 52 learned pairs, and drain both key/descriptor paths
  with no current-cycle M33 work;
- PIO0/DMA can retain passive evidence without driving the bus.

Therefore the real-time core may prepare, supervise, and refill the data plane,
but it is not the default no-wait response engine.

## Logical ownership

### PIO and DMA: hard-real-time data plane

PIO/DMA owns anything that must complete inside the current V30 cycle:

- CLK generation and controlled LOW stop;
- ASTB/T1 address and cycle qualification;
- scattered AD0-AD15 output and input capture;
- `PINDIRS` ownership and release;
- prestaged ROM and hot-RAM response;
- live RAM-write capture and deterministic replay;
- prestaged INTA vector response;
- raw passive trace transport.

No current-cycle path may depend on `printf`, USB callbacks, a filesystem,
dynamic allocation, an inter-core reply, or an unbounded lock.

### Real-time M33 role: control plane

The real-time role owns policy and state that determine future CPU-visible
behavior:

- configure and arm PIO/DMA engines;
- compile response keys, descriptors, and hot-memory slots before use;
- maintain the address map and CPU-visible device registers;
- supervise deterministic cache/refill work outside the active miss cycle;
- maintain PIC/PIT/PPI state and prestage the next interrupt response;
- own RESET, future READY policy, timeouts, and terminal safe state;
- publish lightweight counters and raw events without formatting;
- detect starvation, stale descriptors, unqualified drive, and ownership faults.

The real-time role must not synchronously wait for the service role while the
V30 is running. If required data is unavailable on the current HAT, the cycle
must follow an explicitly validated high-Z/failure policy. On V3.0, controllable
READY may permit a separately bounded slow-path contract.

### Service M33 role: asynchronous services

The service role owns work whose latency may be tens of microseconds or longer:

- USB CDC, command parsing, and human-readable output;
- trace decode, timestamps, formatting, and persistence;
- Flash/ROM image validation and management;
- PSRAM and MicroSD bulk transfers;
- disk images, read-ahead, and write-back policy;
- USB keyboard host processing;
- display interpretation, rendering, and scanout preparation;
- diagnostics, configuration, and management UI.

The service role may prepare buffers and publish state, but it may never be a
synchronous dependency of a no-wait V30 transaction.

## Peripheral ownership

Device ownership is split at the CPU-visible timing boundary rather than by
whole device type.

| Function | Hard data plane | Real-time role | Service role |
|---|---|---|---|
| ROM/RAM current-cycle response | PIO/DMA | prepare/refill/map | image management |
| RAM write | PIO capture | commit/map/supervise | bulk backing |
| PIC/INTA | prestaged vector and pin timing | IRR/ISR/IMR/priority/EOI | diagnostics |
| PIT | optional hardware event path | counter/register/IRQ state | statistics |
| PPI | prestaged register response | CPU-visible registers | host backend |
| Keyboard | queue response/IRQ timing | consume CPU-facing queue | USB producer |
| Video memory | deterministic capture | address/register state | render/scanout |
| Disk interface | fast status/cache response | command state/cache ownership | SD/image I/O |

Putting "memory" on one core and "peripherals" on the other is prohibited when
it inserts a core-to-core round trip into CPU-visible timing.

## Inter-core contract

Prefer single-writer ownership and bounded single-producer/single-consumer
rings.

```text
trace ring:       producer = PIO/DMA or real-time role
                  consumer = service role

keyboard ring:    producer = service role
                  consumer = real-time role

command ring:     producer = service role
                  consumer = real-time role at safe boundaries
```

Required properties:

- queue capacity and element layout are compile-time constants;
- producer publishes payload before the write index with an explicit memory
  barrier; consumer reads the index before payload;
- queue-full behavior is non-blocking and observable;
- trace/log overflow increments a counter and drops records rather than
  stalling the V30;
- Core-local pointers and mutable device objects are not transferred by
  reference without an ownership protocol;
- RESET and bus ownership have one writer: the real-time role;
- inter-core FIFO is reserved for boot/doorbell/control, not bulk trace data;
- neither role takes an unbounded mutex needed by the other.

## Failure isolation

The service role is optional to continued V30 execution. These faults must not
change bus timing or ownership:

- USB host disconnect or CDC backpressure;
- service-core stall or disabled heartbeat;
- full trace/log ring;
- MicroSD timeout;
- display underrun;
- malformed CLI command.

The real-time role may report or later reset the service role, but it must keep
the V30 data plane deterministic and retain the terminal safety policy.

## Physical core-number assignment

The logical roles are fixed; the numbered assignment is provisional. Measure
both placements before locking it:

- PIO/DMA IRQ routing and latency;
- USB IRQ affinity and SDK constraints;
- shared SRAM bank contention;
- DMA versus instruction/data traffic;
- XIP/Flash stalls;
- worst-case queue publication and consumption latency;
- V30 response deadline and trace integrity under service load.

The accepted placement becomes a separate ADR. Until then, code and documents
use `realtime_core` and `service_core`, not assumptions tied to `core0` or
`core1`.

## Introduction plan

### DC-A: non-driving infrastructure

- start the second M33 role without changing any V30 response;
- add bounded SPSC trace and command rings;
- keep RESET and bus ownership on the existing real-time role;
- prove boot, idle, saturation, overflow, and service-core-stall behavior.

The dedicated `pc1c_dual_core_foundation` target implements this gate without
changing the accepted `pc1c_multi_slot_ram` target. Its provisional placement
is Core0=realtime and Core1=service. Core1 has no GPIO, PIO, DMA, RESET, or CDC
authority: after the SDK boot handshake it accesses only shared SRAM rings,
phase/ack words, and a heartbeat.

The test performs these checks around the unchanged B2-C run:

1. start Core1 and observe a changing heartbeat;
2. fill a 64-word realtime-to-service trace ring and verify ordered drain;
3. attempt 16 additional writes while full and prove counted, non-blocking
   drops;
4. transfer 32 ordered service-to-realtime command words;
5. stop Core1 for the entire Epoch-B V30 regression;
6. prove B2-C still passes while the heartbeat remains frozen, then resume
   Core1 and prove the heartbeat restarts.

Passing a local build is not physical acceptance. DC-A remains open until its
CDC output reports `DC-A RESULT = PASS` together with the complete B2-C PASS
and terminal RESET-high, CLK-low, AD-high-Z state.

### DC-B: trace and USB separation

- real-time side publishes raw fixed-size records only;
- service side performs decode, formatting, and CDC output;
- remove live-run `printf` and USB work from the real-time side;
- demonstrate unchanged PC1-C regression output with CDC connected,
  disconnected, and backpressured.

### DC-C: image and console services

- validate and copy Flash ROM images outside RESET release;
- add CDC commands through a bounded command queue;
- accept commands only at explicit safe boundaries;
- retain descriptor-fed and same-run RAM targets as regressions.

### DC-D: storage, keyboard, and display

- service role owns slow backend work;
- real-time role exposes only prepared CPU-visible state;
- every cache miss has a documented current-HAT or V3.0 READY policy.

## Acceptance gates

A dual-core change is accepted only when all relevant items pass:

- all pre-existing PC1-B/PC1-C CPU-visible checks remain unchanged;
- zero response deadline misses and zero unqualified drives;
- RESET-high, CLK-low, AD-high-Z terminal state remains guaranteed;
- Core 1/service-role stall does not stop or corrupt the V30 regression;
- USB CDC backpressure does not alter passive bus traces;
- queue overflow is counted and non-blocking;
- role ownership and memory-ordering rules are documented in code;
- the measured core placement is recorded before it becomes a platform ABI.

## Relationship to the accepted RAM gate

C0C1-B2-C passed physically on 2026-08-21 with two independently addressed
live words, low/high byte-lane coherence, 52/52 qualified pairs, zero DMA/FIFO
residue, and a safe terminal state. DC-A is therefore the next infrastructure
gate. It remains strictly non-driving and cannot affect current-cycle response
timing; `pc1c_multi_slot_ram` remains the permanent CPU-visible regression.
