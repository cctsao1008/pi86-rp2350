# Host-Managed Bare-Metal Physical Processor Runtime

**Status:** Canonical architecture  
**Project:** `pi86-rp2350`

## 1. Definition

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**
>
> *A modern remote-processor runtime for a vintage physical CPU.*

The physical Intel 8086 or NEC V30 executes the workload and remains the execution authority. The Host manages the runtime. The RP2350 owns shared resources and bridges Host-side control to the physical processor through a processor-facing realtime data plane.

This is not CPU emulation and it is not a conventional PC architecture.

## 2. Canonical system planes

The architecture is organized around execution ownership and timing boundaries:

```text
User / AI / engineering tools
            |
            v
Host control plane
  CLI / Web / Remote / RP86
            |
            | USB Host Protocol
            v
RP2350 service / policy plane
  M33: lifecycle / ownership / storage / telemetry / policy
            |
            | prepared state and bounded handoff
            v
RP2350 realtime data plane
  PIO + DMA: bus / memory / I/O / INTA / clock timing
            |
            | physical multiplexed processor bus
            v
Intel 8086 / NEC V30
  physical execution plane
  native machine-code execution authority
```

The critical rule is:

> **An active processor bus cycle must not depend synchronously on PC software, USB latency, filesystem work, or other unbounded software latency.**

## 3. Fixed architectural roles

### Host — control plane

The Host owns user intent and runtime orchestration. It can:

- transfer and select workloads;
- start, stop, inspect, and restart execution;
- provide stdin/stdout and file operations;
- read status, trace, results, and fault information;
- apply timeouts or restart policy.

The reference Host runtime is RP86. Python is the reference client, but the Host Protocol is language-independent.

The Host is never part of the synchronous physical-processor bus path.

### RP2350 M33 — service and policy plane

The M33 cores own policy and resource management around the processor:

- workload lifecycle and ownership;
- memory allocation and staging;
- storage and filesystem services;
- mailbox and telemetry state;
- Host Protocol handling;
- configuration of processor-facing realtime resources.

M33 software may prepare state for the realtime plane, but variable-latency M33 work is not a valid current-cycle responder for a fixed-`READY` Intel bus transaction.

### RP2350 PIO + DMA — realtime data plane

PIO and DMA form the processor-facing timing plane. This plane is responsible for bounded operations such as:

- processor bus capture and classification;
- prepared memory and I/O responses;
- dynamic memory transfer paths when their timing is proven;
- interrupt acknowledge and vector delivery;
- processor clock generation;
- deterministic GPIO ownership and release.

The realtime plane exists specifically to keep active bus transactions independent of Host and service-plane latency.

### Intel 8086 / NEC V30 — physical execution plane

The installed physical processor:

- fetches and executes native IA-16 machine code;
- owns architectural registers and control flow;
- uses assigned code, data, stack, heap, and shared-memory regions;
- requests runtime services through defined processor-visible interfaces;
- may complete, fault, hang, or time out like any real bare-metal processor.

The physical processor is not a peripheral that merely reports simulated execution. It is the workload execution authority.

## 4. Runtime model

```text
Load -> Run -> Communicate -> Observe
                              |
                       Exit / Fault / Timeout
                              |
                           Restart
```

BIOS, DOS, ELKS, boot sectors, PC-compatible devices, and operating systems may be loaded as workloads or experiments. They are not architectural prerequisites of the runtime.

## 5. Workload and launch model

Native processor software lives under `processor/` and is built as real IA-16 machine code.

The production workload pipeline is:

```text
C / ASM source
      |
      v
Open Watcom / NASM
      |
      v
OBJ
      |
      v
WLINK
      |-- .bin
      |-- .map
      `-- .lnk
      |
      v
scripts/package_workload.py
      |
      v
.P86W
      |
      v
Host runtime
      |
      v
RP2350
      |
      v
physical Intel 8086 / NEC V30
```

The workload contract carries explicit processor launch metadata such as load address, initial `CS:IP`, stack state, image size, CRC, flags, and other processor-visible metadata.

## 6. Resource and ownership model

The RP2350 is the single low-level resource owner. Host and physical processor may share content, but they do not share low-level controller ownership.

```text
Host request -----------+
                        v
                  RP2350 owner
                 /      |       \
             memory   storage   runtime/bus state
                        ^
processor service request ----+
```

| Resource | Architectural role |
|---|---|
| RP2350 Internal SRAM | firmware and realtime state, workload backing, processor-visible memory, and shared-memory regions |
| External PSRAM | optional capacity tier for larger workloads, bulk shared memory, snapshots, and cache/refill backing |
| External NOR Flash | firmware region plus shared persistent storage |
| SD Card | optional removable storage |

Logical memory ownership and physical realtime service feasibility are separate concerns. A region may be assigned as processor-visible backing while its processor-specific no-wait realtime access path is still under validation.

The physical processor never directly owns USB, FAT, NOR Flash, SD, PSRAM controllers, PIO, or DMA.

## 7. Processor-visible boundary

Processor software should depend only on architectural interfaces such as:

- processor ABI;
- memory map;
- I/O map;
- interrupt ABI;
- shared mailbox;
- workload-visible services.

Processor code should not depend on:

- USB;
- Python or the Host broker implementation;
- RP2350 internal implementation details;
- PIO state-machine implementation details.

Cross-plane contracts are authoritative at their defined boundaries rather than being independently redefined in C, Python, and NASM.

## 8. Physical timing boundary

The original Pi86 HAT keeps processor `READY` asserted. The current hardware therefore cannot insert conventional processor wait states during an active cycle.

This makes the processor clock contract and realtime response path architectural constraints rather than implementation details.

### Intel 8086

For Intel 8086 general execution, the canonical contract is:

```text
continuous in-spec processor clock
              |
              v
fixed READY / no ordinary Tw escape
              |
              v
all supported active bus cycles
must complete through a bounded realtime path
              |
              v
PIO / DMA / prepared hardware state
```

The installed Intel `P8086-2` requires a continuously running clock at or above the documented minimum frequency. `FREE_RUNNING` is therefore the architectural basis for compliant Intel general execution.

`CLOCK_STEPPED` remains useful for bring-up, diagnostics, historical validation, and empirical experiments, but it is **not** a compliant Intel 8086 general-execution mode because software may hold `CLK` low for an unbounded interval.

Previously accepted Intel results obtained at 1 MHz or under stopped-clock execution remain valid empirical observations of the tested hardware. They are not reclassified as vendor-compliant operating points.

### NEC V30

The NEC V30 shares the same Host/RP2350 runtime architecture and physical Pi86 interface, but its minimum-frequency and clock-stop guarantees are a separate processor-specific contract.

The project does not assume that Intel 8086 and NEC V30 timing guarantees are interchangeable.

> **Shared runtime architecture does not imply a shared silicon timing contract.**

## 9. Realtime service rule

The processor-facing path is deliberately split from slower services:

```text
Host / USB / filesystem / storage
              |
              | asynchronous or staged
              v
       RP2350 service plane
              |
              | prepared bounded state
              v
       PIO + DMA realtime plane
              |
              v
        active CPU bus cycle
```

A processor transaction that cannot be satisfied by the bounded realtime path must not be made correct by waiting on Host software or by indefinitely stopping an Intel 8086 clock. It must instead be rejected, faulted observably, staged differently, or require a future hardware mechanism such as controllable `READY`.

Dynamic arbitrary Internal-SRAM service under the Intel continuous-clock contract is an implementation feasibility question beneath this architectural rule; the architecture does not claim it complete until timing evidence proves it.

## 10. Verification architecture

Verification is layered so software models eliminate hypotheses without replacing physical evidence:

```text
structural / contract checks
            |
            v
Host / firmware tests
            |
            v
IA16 binary execution lab
            |
            v
integration / realtime-path tests
            |
            v
physical Intel 8086 / NEC V30
```

### IA16 Lab

The IA16 Lab consumes production machine-code artifacts and provides instruction, register, memory, and symbol-level analysis.

It is:

> **a binary execution microscope for production IA-16 software**

It is not an RP86 emulator and it does not replace physical validation.

### Physical evidence authority

The final evidence authority remains the real processor under the exact documented electrical, clock, firmware, and workload conditions.

## 11. Failure boundary

The platform guarantees ownership and electrical discipline, not workload success. On a workload fault or timeout it should:

1. preserve available state and trace;
2. keep Host control infrastructure alive where possible;
3. report the observed outcome;
4. wait for explicit inspection, restart, stop, or reload.

Unsupported realtime bus transactions must fail observably rather than silently depending on unbounded software timing.

## 12. Architectural position

The project is best summarized as:

```text
Intel 8086 / NEC V30
    = physical execution authority

RP2350 PIO + DMA
    = realtime processor-bus data plane

RP2350 M33
    = service and policy plane

Host
    = orchestration, tooling, and remote control

IA16 Lab
    = pre-silicon binary microscope

Physical silicon
    = final evidence authority
```

Or as one sentence:

> **pi86-rp2350 is a host-managed physical IA-16 runtime with an RP2350 service plane and a hardware-paced PIO/DMA realtime bus data plane.**

The architecture deliberately avoids turning the project into:

- an RP2350 CPU emulator;
- a PC-driven synchronous processor runtime;
- a design in which Host software participates in active bus-cycle timing;
- a design in which the physical Intel 8086 or NEC V30 is merely a peripheral.

## 13. Durable principles

1. **Execution ownership first.** Source location should reflect where code executes and which plane owns it.
2. **Physical processor first.** The physical CPU executes native workloads and remains final execution authority.
3. **RP2350 as companion chipset, not emulator.** M33 manages services; PIO/DMA owns bounded realtime processor interaction.
4. **Host outside the synchronous bus path.** USB and Host software must never become active-cycle timing dependencies.
5. **Processor-specific timing contracts.** Intel and NEC share the runtime architecture but their silicon timing guarantees are audited independently.
6. **Production artifacts are shared evidence.** Deployment artifacts should also feed binary-level analysis where possible.
7. **Simulation falsifies hypotheses; hardware closes them.** Use software execution models to reduce uncertainty, then validate remaining claims on silicon.
8. **Cross-plane contracts have explicit authority.** ABI and binary definitions must not drift independently across languages.
9. **Product code and engineering tools remain separate.** Debug infrastructure must not become a hidden runtime dependency.
10. **Physical silicon remains final authority.** Architectural claims remain grounded in the real processor under stated operating conditions.

## 14. Detailed contracts and decisions

- [`host_runtime.md`](host_runtime.md) — Host runtime, permission, and ownership contract
- [`memory.md`](memory.md) — memory and storage ownership
- [`hardware.md`](hardware.md) — board resources, signal mapping, and physical ownership
- [`intel_8086_clock_contract.md`](intel_8086_clock_contract.md) — Intel clock-contract audit and implications
- [`../reference/host_protocol.md`](../reference/host_protocol.md) — Host/RP2350 wire semantics
- [`../reference/host_runtime_shell.md`](../reference/host_runtime_shell.md) — Host shell command surface
- [`../adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](../adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — runtime identity
- [`../adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md`](../adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md) — processor scope
- [`../adr/0010-adopt-free-running-and-clock-stepped-execution.md`](../adr/0010-adopt-free-running-and-clock-stepped-execution.md) — historical clock-mode decision
- [`../adr/0011-constrain-intel-8086-to-continuous-in-spec-clock.md`](../adr/0011-constrain-intel-8086-to-continuous-in-spec-clock.md) — current Intel execution-clock constraint
