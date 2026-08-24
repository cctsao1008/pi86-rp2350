# Host Protocol

## 1. Scope

This document defines the canonical host-facing contract for `pi86-rp2350`.

The Host Protocol connects the **Runtime Controller** to the RP2350
**Companion Resource and Bus Controller**. The physical NEC V30 remains the
**Bare-Metal Remote Physical Processor**.

The project does **not** require a particular host programming language, SDK, CLI, application framework, Web UI, or AI service.

> **The project defines the wire protocol. Host software is an implementation choice.**

The minimum host interface is:

```text
Host
  |
  +-- USB HID  - structured command / response
  |
  `-- USB CDC  - log / diagnostic / observation stream
        |
        v
      RP2350
```

Sample Python, C, Rust, or other host programs may be provided to demonstrate the protocol, but they are not architectural dependencies.

## 2. Design principles

The host protocol follows these rules:

- runtime operations are language-independent;
- HID is the initial command/response transport, not the definition of the operations themselves;
- CDC is an observation stream, not a second control protocol;
- host latency never enters a current V30 bus cycle;
- a host disconnect is not by itself a runtime-integrity fault;
- malformed or unsupported Host requests must not silently change runtime state;
- bulk transfer mechanisms may evolve without changing machine-operation semantics.

## 3. HID command/response transport

The existing validated 64-byte record framing is retained as the version-1 transport foundation.

Every version-1 record is 64 bytes and uses little-endian multibyte fields:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0` | 1 | `version` | protocol version |
| `1` | 1 | `type` | record or operation class |
| `2` | 2 | `flags` | version-specific flags |
| `4` | 4 | `sequence` | transaction identity |
| `8` | 2 | `length` | valid payload bytes |
| `10` | 2 | `status` | result/error status |
| `12` | 52 | `payload` | payload followed by padding |

Equivalent packed layout:

```text
<BBHIHH52s
```

The exact currently validated record mechanics, sequence rules, retry behavior, and historical Companion Service message types remain documented in [`companion_service_abi.md`](companion_service_abi.md).

This document defines generic runtime operations rather than BIOS-, OS-, AI-,
or workload-specific semantics.

## 4. Operation groups

The Host Protocol exposes operations required to load, execute, communicate
with, inspect, and restart the physical V30 runtime.

### System

Examples:

```text
SYSTEM_INFO
CAPABILITIES_GET
```

Returned capability information should describe available physical resources and implemented functions rather than force host software to assume a fixed configuration.

Examples include External PSRAM availability/capacity, filesystem availability, optional SD presence, supported clock range, and protocol version.

### Runtime and processor control

Prefer operations with explicit physical semantics:

```text
RESET_ASSERT
RESET_RELEASE
CLOCK_SET
CLOCK_START
CLOCK_STOP
STATE_GET
```

Avoid ambiguous commands such as `HALT` until their physical meaning is explicitly defined. V30 `HLT`, stopping the generated clock, and asserting RESET are different machine actions.

A minimal runtime-state model is:

```text
EMPTY
LOADED
RUNNING
EXITED
STOPPED
FAULT
TIMEOUT
```

### Memory

Examples:

```text
MEM_READ
MEM_WRITE
MEM_MAP_GET
```

Host memory operations address assigned V30-visible memory through RP2350
ownership. They do not grant the Host raw ownership of RP2350 SRAM, PSRAM
metadata, or bus-engine state.

Memory-map semantics and physical backing are defined in [`memory_architecture.md`](memory_architecture.md).

### Persistent storage

Examples:

```text
FS_LIST
FS_READ
FS_WRITE
FS_DELETE
FS_RENAME
FS_SYNC
```

The RP2350 remains the sole filesystem owner. Host requests are serialized by firmware rather than directly mounting or mutating the Flash filesystem.

### Workload

Examples:

```text
WORKLOAD_LOAD
WORKLOAD_RUN
WORKLOAD_STOP
WORKLOAD_RESTART
```

The initial workload model is intentionally small: raw native V30 binary plus minimal launch metadata.

`WORKLOAD_LOAD` verifies and stages the native image and launch metadata.
`WORKLOAD_RUN` releases the physical V30 into execution. `WORKLOAD_STOP`
and `WORKLOAD_RESTART` preserve the Host's ability to recover from a workload
exit, fault, hang, or timeout.

### Trace / observation control

Examples:

```text
TRACE_START
TRACE_STOP
TRACE_READ
```

Trace-control requests configure future observation. They must not create a synchronous host dependency in the V30 current-cycle path.

The exact version-1 opcode assignments and payload layouts should be added only when each operation is implemented and tested. This document defines semantics first and intentionally does not reserve speculative numeric opcode values.

## 5. Command completion and errors

The existing sequence-based request/reply model is retained as a useful protocol property:

- a request has a nonambiguous sequence;
- its response carries the same sequence;
- malformed length/version requests are rejected;
- unsupported operations return an explicit error;
- retry semantics must not execute a mutating operation twice;
- incomplete records are never published as complete operations.

Two error classes are architecturally distinct.

### Management errors

Examples:

- invalid command;
- invalid range;
- file not found;
- unsupported capability;
- workload checksum failure.

These return an error and leave the machine in a defined state without silently forcing a machine fault.

### Machine-integrity faults

Examples:

- deterministic response starvation;
- illegal bus ownership;
- DMA/PIO state corruption;
- critical memory-publication inconsistency.

These are runtime/platform faults, not ordinary Host Protocol errors. Firmware
enters the electrical safe state defined by the architecture and reports
retained diagnostics when possible.

## 6. Data transfer

Version 1 may use repeated HID records for data larger than one 52-byte payload.

However:

> **Machine-operation semantics must not depend on HID report size or fragmentation.**

A future USB bulk endpoint or other payload transport may therefore accelerate large workload, filesystem, snapshot, or trace transfers while retaining the same logical operations.

Adding such a transport is an optimization, not a new host software architecture.

## 7. CDC observation stream

CDC provides human-readable or lightly structured observation outside the deterministic V30 path.

It is intended for:

- firmware logs;
- machine-state changes;
- faults;
- workload execution events;
- diagnostic summaries;
- trace summaries.

A simple baseline format is sufficient:

```text
timestamp level source message
```

For example:

```text
123456 INFO  SYS  reset released
123500 INFO  V30  workload started
123620 WARN  BUS  unsupported cycle
123700 ERROR BUS  deterministic fault
```

CDC output may be delayed, dropped with accounting, or unavailable without becoming a synchronous dependency of V30 execution.

A binary high-volume trace channel should not be added to CDC merely to avoid defining an appropriate bulk transport later.

## 8. Host disconnect and independence

The Host is a control and observation client, not a realtime component.

If USB disconnects while a workload is running:

```text
Host unavailable
       |
       v
RP2350 continues physical runtime operation
       |
       v
V30 may continue executing
```

An active workload may explicitly declare a dependency on a future host service, but that is a workload/service capability and must not become a hidden core-machine dependency.

## 9. Host sample code

Repository tools and scripts are reference users of the protocol, not normative SDK layers.

Sample code should demonstrate:

- HID record encode/decode;
- command/response sequencing;
- CDC reading;
- representative memory, storage, workload, and state operations as they become implemented.

Host APIs may be convenient, for example `machine.reset()` or `machine.fs.list()`, but such APIs are not part of the wire contract and may differ between languages.

## 10. Relationship to the Companion Service ABI

[`companion_service_abi.md`](companion_service_abi.md) records the validated 64-byte Host Bridge framing and V30 mailbox mechanism used by earlier Companion Service experiments.

The record framing remains useful and may be reused by this Host Protocol. The V30 mailbox, BIOS `INT 60h`, PIT heartbeat, and persistent-runtime behavior in that document are **optional validated mechanisms**, not requirements of the core Host Protocol defined here.

Historical validation records using those mechanisms remain authoritative for the tests they describe.

## 11. Related documents

- [`architecture.md`](architecture.md) - overall system architecture
- [`memory_architecture.md`](memory_architecture.md) - memory terminology, V30 Memory Map, and backing resources
- [`companion_service_abi.md`](companion_service_abi.md) - validated Host Bridge/Companion Service v1 record and mailbox path
- [`host_runtime_architecture.md`](host_runtime_architecture.md) - detailed runtime contract
- [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) - current architecture decision
