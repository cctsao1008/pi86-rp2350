# ADR 0007: Adopt the Host-Constructed V30 Machine Model

- Status: Accepted
- Date: 2026-08-24

## Context

The project has validated physical NEC V30 execution, deterministic PIO/DMA bus response, address-qualified ROM/RAM behavior, interrupts, persistent runtime experiments, and a USB HID/CDC host bridge.

Earlier architecture work used PC-oriented milestones such as BIOS, PIC/PIT services, DOS, and ELKS as progressively more complex workloads. Those remain useful compatibility and validation targets, but they are not necessary to define the machine itself.

The project now needs a smaller architectural core that does not assume an IBM PC boot chain, a BIOS API, an operating system, or a specific host programming language.

The existing hard realtime boundary remains unchanged:

> **PIO/DMA and bounded on-chip state own current-cycle V30 timing. Arm software, USB, storage, and host tools operate outside that active-cycle path.**

## Decision

The system is defined by three actors:

```text
Host
  |
  | USB HID + CDC
  v
RP2350 Machine Platform
  |
  | deterministic physical bus
  v
Physical NEC V30
```

The responsibilities are:

- **Host** - requests machine operations and consumes observations. The host language, SDK, script, program, AI agent, or UI is not part of the architecture.
- **RP2350 Machine Platform** - constructs, owns, controls, and observes the V30 execution environment.
- **Physical NEC V30** - executes native workloads and owns its architectural CPU state.

The governing model is:

> **The RP2350 constructs the V30-visible machine before execution; the V30 then executes inside that prepared environment.**

### Host contract

The canonical host contract is a small language-independent USB interface:

- **USB HID** - structured command/response transport;
- **USB CDC** - log, diagnostic, and observation stream.

The project may provide sample host code, but no Python, C, Rust, Web, AI, or other host SDK is architecturally required.

Protocol operation semantics must not depend on the current HID report size so that a future bulk-data transport can be added without redefining machine operations.

### RP2350 firmware scope

The minimum complete RP2350 firmware has six responsibilities:

1. V30 bus engine;
2. machine control;
3. memory mapping/backing management;
4. workload preparation and reset handoff;
5. host interface;
6. persistent storage.

BIOS, DOS, ELKS, PIC/PIT/PPI compatibility, PC memory maps, V30 filesystem services, and other device models are optional capabilities or workloads rather than core firmware requirements.

### Reset handoff

The V30 architectural reset fetch at physical `FFFF0h` is treated as a **Reset Handoff Point**, not as an implicit BIOS entry.

The RP2350 must provide a deterministic instruction source for reset and may prepare a minimal trampoline that establishes workload-required CPU state before transferring control.

The initial launch contract is intentionally small:

- entry `CS:IP`;
- initial `SS:SP`;
- initial `DS`;
- initial `ES`.

Additional arguments or machine descriptors require an explicit later need.

### Memory model

Physical resources and V30-visible semantics are separate concepts.

Canonical resource names are:

- **RP2350 Internal SRAM**;
- **External NOR Flash**;
- **External PSRAM**;
- **SD Card** when present.

The **V30 Memory Map** describes CPU-visible address semantics. A **Backing Resource** describes how an implemented region is materialized.

The minimum useful V30 memory map requires:

- a Reset Handoff Region;
- an Executable Region;
- a Writable RAM Region;
- defined behavior for unmapped addresses.

No IBM PC conventional-memory, VGA, BIOS-ROM, IVT, or device region is required by the core architecture.

### Memory hierarchy

- **RP2350 Internal SRAM** is prioritized for firmware runtime, deterministic bus state, queues/descriptors, and explicitly prepared V30 working windows.
- **External PSRAM** is the target-machine bulk volatile backing/workspace. It is not automatically a valid current-cycle responder.
- **External NOR Flash** holds RP2350 firmware, recovery space, persistent metadata, and a filesystem for machine assets.
- **SD Card** is optional removable bulk storage.

An SRAM + NOR configuration remains useful for bring-up and diagnostics. The target machine configuration includes external PSRAM so bulk volatile machine state does not consume deterministic SRAM capacity.

The initial implementation uses explicit prepared deterministic windows rather than a general-purpose cache hierarchy.

### Filesystem ownership

Persistent Flash storage follows one ownership rule:

> **One filesystem, one owner, multiple clients.**

The RP2350 is the sole filesystem owner. Host access is mediated through the host protocol. V30 file access, if ever required by a workload, is an optional RP2350 service rather than direct filesystem ownership by the V30.

### Failure model

Management errors return an error without silently changing machine state.

If deterministic machine correctness or bus ownership cannot be guaranteed, the firmware enters a safe fault state: assert V30 RESET, place the clock in its defined safe state, release the AD bus to high impedance, and retain diagnostics.

A host disconnect is not by itself a machine-integrity fault; the V30 may continue unless the active workload explicitly depends on a host-side service.

## Consequences

Positive consequences:

- BIOS and operating-system assumptions no longer define the project.
- Host tooling remains small and language-independent.
- RP2350 firmware scope is bounded by six necessary responsibilities.
- Physical memory resources are separated cleanly from the V30 Memory Map.
- Existing deterministic PIO/DMA work remains the core execution mechanism.
- PC compatibility can continue as an optional profile without constraining native workloads.

Costs and risks:

- Workload launch and memory-map contracts must now be explicit rather than inherited from PC conventions.
- External PSRAM integration still requires measured timing and a deterministic staging policy.
- Existing documents and code contain BIOS/PIT/PIC/AI-bridge terminology that must be classified as optional, validated, or historical rather than treated as core architecture.

## Relationship to earlier ADRs

This ADR **supersedes the PC-centric roadmap assumptions** in ADR 0002, including the implication that Mini BIOS, BIOS services, or DOS are the natural architectural progression after deterministic memory.

It does **not** invalidate ADR 0002's measured PIO/DMA findings, the deterministic bus-plane decision, or any historical validation evidence.

ADR 0003 remains authoritative for the `READY`/deterministic-hit constraint.

ADRs 0004 and 0005 remain records of the validated host/companion mailbox path and terminology at that stage. Their mechanisms may be reused, but they do not require the new core architecture to expose an AI-specific or BIOS-mediated runtime.

## Follow-up

Canonical architecture and interface documents should be aligned with this decision while historical validation records remain unchanged.

See:

- [`../architecture.md`](../architecture.md)
- [`../memory_architecture.md`](../memory_architecture.md)
- [`../host_protocol.md`](../host_protocol.md)
- [`0003-require-ready-or-deterministic-hits-for-general-memory.md`](0003-require-ready-or-deterministic-hits-for-general-memory.md)
