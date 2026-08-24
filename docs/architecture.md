# V30 Machine Platform Architecture

## 1. Scope

`pi86-rp2350` builds a programmable machine platform around a **physical NEC V30** using the RP2350 and the original Pi86 V20/V30 HAT.

The architecture has three actors:

```text
Host
  |
  | USB HID + CDC
  v
RP2350 Machine Platform
  |
  | deterministic physical bus
  v
Original Pi86 HAT
  |
  v
Physical NEC V30
```

The V30 executes native code and owns its architectural CPU state. The RP2350 constructs, owns, controls, and observes the surrounding execution environment. The Host requests machine operations and consumes observations.

The central realtime boundary is:

> **PIO/DMA and bounded on-chip state own current-cycle V30 timing. Arm software, USB, storage, and host tools operate outside that active-cycle path.**

This architecture does not require an IBM PC BIOS, an operating system, a PC memory map, or a specific host programming language.

See [`adr/0007-adopt-host-constructed-v30-machine-model.md`](adr/0007-adopt-host-constructed-v30-machine-model.md).

## 2. Governing execution model

The machine is prepared before the V30 is released into execution:

```text
Host request / stored configuration
             |
             v
RP2350 prepares machine state
             |
             +-- V30 Memory Map
             +-- workload image
             +-- deterministic response state
             +-- Reset Handoff
             +-- optional capabilities
             |
             v
        release RESET
             |
             v
      Physical V30 executes
             |
             v
     RP2350 observes results
```

The governing principle is:

> **The RP2350 constructs the V30-visible machine before execution; the physical V30 then executes inside that prepared environment.**

Traditional boot chains such as BIOS -> boot sector -> loader -> operating system are optional compatibility workloads, not the core execution model.

## 3. RP2350 firmware responsibilities

The minimum complete RP2350 firmware has six responsibilities. These are responsibilities, not mandatory software layers.

### V30 Bus Engine

Own the deterministic physical interface:

- V30 clock and phase generation;
- address/control capture and cycle qualification;
- AD0-AD15 direction, drive, release, and high-Z safety;
- prepared memory/I/O read responses;
- write capture;
- deterministic interrupt/INTA response when such a capability is enabled;
- raw observation transport through PIO/DMA.

### Machine Control

Own machine-level physical state such as:

- RESET assertion/release;
- supported clock configuration/start/stop;
- machine state;
- terminal safe state and fault handling.

Prefer explicit physical semantics over ambiguous abstractions. V30 `HLT`, clock stop, and RESET assertion are different operations.

A minimal machine-state model is:

```text
RESET
PREPARED
RUNNING
STOPPED
FAULT
```

### Memory

Own the V30 Memory Map, backing assignments, and deterministic preparation policy.

The V30 Memory Map describes CPU-visible semantics. Physical resources such as RP2350 Internal SRAM and External PSRAM are backing implementation choices.

See [`memory_architecture.md`](memory_architecture.md).

### Workload / Reset Handoff

Prepare a native workload and transfer execution from the V30 architectural reset state to the selected workload.

The V30 reset fetch at physical `FFFF0h` is a **Reset Handoff Point**, not an implicit BIOS entry.

The initial launch contract is intentionally small:

```text
entry CS:IP
initial SS:SP
initial DS
initial ES
```

The RP2350 may prepare a minimal deterministic trampoline that establishes this state before jumping to the workload entry point.

The initial workload format is a raw binary plus minimal launch metadata. Executable-format ecosystems, relocation frameworks, package managers, or operating-system loaders are added only if a demonstrated workload requires them.

### Host Interface

Expose a small language-independent USB contract:

```text
USB HID = structured command / response
USB CDC = log / diagnostic / observation
```

The project defines the wire protocol and may provide sample host code. Python, C, Rust, Web applications, AI agents, CLIs, and other host programs are clients rather than architecture components.

See [`host_protocol.md`](host_protocol.md).

### Persistent Storage

Own persistent machine assets in External NOR Flash, including firmware, recovery reservation, metadata/configuration, and filesystem-backed workloads or data.

SD storage may be added as optional removable bulk storage. Neither Flash filesystem work nor SD access is part of the current-cycle V30 timing path.

## 4. Deterministic bus boundary

The original Pi86 HAT keeps V30 `READY` asserted. A no-wait V30 transaction therefore cannot depend on an unbounded lookup or refill.

A current-cycle path must not synchronously depend on:

- an M33 software decision;
- an inter-core round trip;
- USB callbacks or host latency;
- filesystem operations;
- External NOR Flash access;
- arbitrary External PSRAM latency;
- dynamic allocation or unbounded locks.

Current-cycle state must already be represented by a bounded deterministic mechanism, typically prepared RP2350 Internal SRAM state feeding PIO/DMA.

Unsupported or late responses must remain electrically safe rather than drive stale or speculative data.

See [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

## 5. Memory architecture

Canonical physical resource names are:

```text
RP2350 Internal SRAM
External NOR Flash
External PSRAM
SD Card (optional)
```

The V30 exposes a 20-bit physical address space, but the core architecture does not impose a PC/XT layout.

The minimum useful V30 Memory Map provides:

```text
Reset Handoff Region
Executable Region
Writable RAM Region
Defined Unmapped Behavior
```

The executable and writable regions are selected by the workload/machine configuration rather than fixed by PC convention.

### Resource roles

- **RP2350 Internal SRAM** - firmware runtime, deterministic bus state, queues/descriptors, explicitly prepared V30 windows, short trace/fault state.
- **External NOR Flash** - firmware, recovery capacity, persistent metadata, filesystem, workloads/assets.
- **External PSRAM** - target-machine bulk volatile backing/workspace, workload staging, snapshots, long traces; not an assumed direct current-cycle responder.
- **SD Card** - optional removable bulk storage.

An SRAM + NOR configuration remains useful for bring-up and diagnostics. The target machine configuration includes External PSRAM so bulk volatile machine state does not consume deterministic on-chip SRAM.

The initial implementation uses explicit prepared deterministic windows rather than a general cache hierarchy.

See [`memory_architecture.md`](memory_architecture.md).

## 6. Filesystem ownership

Persistent Flash storage follows one rule:

> **One filesystem, one owner, multiple clients.**

The RP2350 is the sole filesystem owner.

- Host filesystem access is mediated by the Host Protocol.
- RP2350 firmware accesses its own storage directly through the filesystem implementation.
- V30 file access, if required by a future workload, is an optional RP2350-mediated service.

The V30 does not directly mount or own the External NOR Flash filesystem.

## 7. Host interaction

The Host is a control and observation client, not a realtime component.

HID carries structured command/response operations such as machine control, memory access, storage access, workload management, state query, and trace control.

CDC carries logs, diagnostics, state changes, fault reports, and observation summaries.

Host protocol semantics are independent of the host language and should not be tied to HID report size. A future bulk payload transport may be added without redefining machine operations.

A Host disconnect is not by itself a machine-integrity fault. The V30 may continue to execute unless the active workload explicitly depends on an optional host-side service.

See [`host_protocol.md`](host_protocol.md).

## 8. Failure and safe-state model

Two classes of failure are distinguished.

### Management error

Malformed commands, missing files, invalid ranges, unsupported capabilities, or bad workload metadata return an explicit error while leaving the machine in a defined state.

### Machine-integrity fault

If deterministic correctness, bus ownership, or critical published machine state cannot be guaranteed, the RP2350 enters `FAULT` and establishes a safe physical state:

```text
assert V30 RESET
place clock in defined safe state
release AD bus to high-Z
retain diagnostics
```

The governing rule is:

> **If deterministic correctness is uncertain, stop the physical machine safely.**

## 9. RP2350 core roles

PIO/DMA own active V30 cycles. M33 work is separated by responsibility rather than permanent Core 0/Core 1 numbering.

A realtime role prepares and supervises future deterministic state. A service role handles USB, storage, formatting, trace processing, and other asynchronous work.

The exact core-number assignment is an implementation choice based on measured IRQ behavior, SDK requirements, SRAM-bank contention, and service load.

See [`dual_core_partitioning.md`](dual_core_partitioning.md).

## 10. Optional capabilities and compatibility

The following are not required to form the core machine:

- BIOS APIs;
- DOS or ELKS;
- 8259A/PIC, PIT, or PPI compatibility;
- IBM PC memory maps;
- disk-image boot paths;
- display or keyboard compatibility;
- V30 filesystem services;
- persistent runtime/heartbeat mechanisms;
- AI-specific services.

They remain valuable as optional workloads, compatibility profiles, or validated mechanisms.

Historical BIOS, PIC/PIT, mailbox, ELKS, and PC-compatibility documents and validation records remain authoritative for the tests they describe; they do not define the new minimum architecture.

## 11. Implementation lineage

The repository contains validated progression from software-stepped bring-up to PIO/DMA deterministic clocking, qualified response, SRAM-backed native execution, interrupts, persistent runtime experiments, and HID/CDC host interaction.

Those results remain engineering evidence. Architecture cleanup must not rewrite measured clocks, captured outputs, gate names, or historical acceptance records.

The current architecture changes the interpretation of those mechanisms: they are reusable capabilities around a smaller host-constructed V30 machine rather than mandatory steps toward a PC-compatible endpoint.

## 12. Related documents

- [`memory_architecture.md`](memory_architecture.md) - physical memory resources, V30 Memory Map, backing policy
- [`host_protocol.md`](host_protocol.md) - canonical Host/RP2350 wire contract
- [`hardware_contract.md`](hardware_contract.md) - physical interface contract
- [`pin_mapping.md`](pin_mapping.md) - GPIO/header/V30 signal mapping
- [`dual_core_partitioning.md`](dual_core_partitioning.md) - realtime/service ownership
- [`companion_service_abi.md`](companion_service_abi.md) - validated historical/optional Companion Service record and V30 mailbox mechanism
- [`adr/0007-adopt-host-constructed-v30-machine-model.md`](adr/0007-adopt-host-constructed-v30-machine-model.md) - current machine-model decision
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - deterministic memory/READY policy
