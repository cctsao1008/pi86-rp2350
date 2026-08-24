# pi86-rp2350

**A physical NEC V30 with Raspberry Pi RP2350 PIO and DMA acting as a deterministic, programmable companion chipset.**

`pi86-rp2350` preserves a real NEC V30 CPU and the original Pi86 V20/V30 HAT interface while replacing the Linux/GPIO polling model with a bare-metal RP2350 architecture built around PIO, DMA, internal SRAM, explicit ownership, retained physical evidence, and host-side automation.

The project is not an x86 emulator and is not intended to become another complete retro-PC implementation. Its focus is the boundary around the physical processor: how that boundary can be made deterministic, observable, controllable, programmable, and usable by modern tools and AI agents.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 (V30) on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## Purpose

The RP2350 is treated as a **programmable chipset around a physical 8086-class processor**, not as a faster host that bit-bangs the V30 bus in software.

The central research question is:

> **How far can a modern programmable MCU turn a real legacy CPU into a physically verifiable, cycle-aware, programmable, and AI-operable computing environment?**

The project therefore concentrates on four properties:

- **Physical** - the NEC V30 remains the processor executing native code.
- **Cycle-aware** - bus activity, timing, ownership, and response classes are explicit and measurable.
- **Programmable** - memory, I/O, interrupts, runtime services, and compatibility behavior can be supplied by a software-defined companion chipset.
- **AI-operable** - structured evidence and bounded control interfaces are designed so conventional tools and AI agents can observe, configure, and analyze experiments without entering the realtime bus path.

PC compatibility, BIOS execution, DOS, ELKS, diagnostic ROMs, and other native software are useful workloads and compatibility profiles. They are not the definition of project completion.

## Architectural invariants

The implementation may evolve, but these rules define the project:

1. **The NEC V30 remains physical.** RP2350 firmware supplies surrounding chipset functions; it does not replace the processor.
2. **Current-cycle V30 timing belongs to deterministic hardware-assisted paths.** PIO, DMA, and bounded on-chip state own the critical response path; an Arm core is not required to resolve an active physical response cycle.
3. **The Arm cores form control and service roles.** They prepare immutable state, supervise bounded engines, handle exceptions, and provide asynchronous services outside the hardest timing path.
4. **Host transport is not a bus-timing mechanism.** USB, Python, storage, display, and AI clients remain outside the current V30 bus cycle.
5. **Physical evidence and interpreted results remain separate.** A visible application result is not sufficient proof of correct physical execution.
6. **Unsupported or late cycles fail safely.** The response engine must not drive stale, speculative, or unqualified data onto the V30 bus.
7. **Target architecture and validated capability are stated separately.** A planned memory, peripheral, or compatibility role is not treated as implemented until physical evidence satisfies its acceptance contract.
8. **AI does not define correctness.** PASS/FAIL remains based on explicit invariants and retained evidence that can be evaluated independently of an AI model.

The timing hierarchy is:

```text
PIO / DMA / bounded SRAM state = hard realtime data plane
Realtime control role          = bounded supervision and publication
Service role                   = asynchronous system services
Host software                  = policy, tools, images, experiments
AI agent                       = reasoning over host-visible state and evidence
```

## System architecture

```text
                 conventional host tools / AI agents
                              |
                  provider-neutral host API
                              |
              observation / control / experiment
                              |
                              v
              +------------------------------------+
              |        Waveshare RP2350-PiZero     |
              |         Raspberry Pi RP2350B       |
              |                                    |
              |  service plane                     |
              |    USB / console / storage / tools |
              |                                    |
              |  control plane                     |
              |    immutable state / supervision   |
              |                                    |
              |  deterministic bus plane           |
              |    PIO capture / qualification     |
              |    PIO qualified response          |
              |    PIO V30 clock / phase           |
              |    DMA SRAM <-> PIO transport      |
              +-----------------+------------------+
                                |
                    Raspberry Pi 40-pin physical ABI
                                |
                    Original Pi86 V20/V30 HAT
                                |
                                v
                     +----------------------+
                     |     Physical V30     |
                     |    NEC D70116C-8     |
                     | native x86-class code|
                     +----------------------+
```

The architectural contract is role-based. Exact state-machine or Core 0/Core 1 placement may change without changing ownership rules.

## Deterministic bus plane

PIO and DMA are the principal architectural enablers. They provide hardware-like response paths that remain independent of host activity and normal Arm instruction latency.

### Passive observation

A passive PIO path observes physical V30 address, control, and data phases without taking ownership of the AD bus. DMA retains observations in SRAM so software can decode and analyze evidence later without perturbing the measured cycle.

### Qualified response

A response path drives the AD bus only when the current physical cycle matches a qualified response condition. Bus direction and release are part of the deterministic response contract rather than ordinary software-side GPIO activity.

### Clock and phase authority

PIO owns V30 clock generation and the phase relationships used by accepted response engines. Clock behavior is treated as part of the physical experiment configuration, not as an implicit software delay.

### DMA transport

DMA moves prepared keys, descriptors, response data, and retained observations between SRAM and PIO FIFOs. DMA is transport; it does not turn an Arm core into a current-cycle responder.

The defining boundary is:

> **The deterministic bus plane answers the active V30 cycle. Arm software prepares, supervises, and consumes state around it.**

## Control and service roles

The RP2350 Arm cores are separated by responsibility so higher-level services cannot silently enter the V30-critical timing path.

- **Realtime control role** - bus-epoch supervision, immutable publication, bounded timing-sensitive coordination, failure containment, and preparation outside the current cycle.
- **Service role** - USB-facing services, formatting, retained trace processing, debugging, storage, image management, console, and other asynchronous work.

Cross-role communication must use bounded ownership transfer. Queue-full, stalled-service, and backpressure behavior must fail visibly rather than perturb deterministic V30-visible behavior.

See [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md).

## Memory architecture

The V30 has a 20-bit physical address space (`00000h`-`FFFFFh`). The companion-chip architecture separates deterministic current-cycle state from bulk backing storage.

| Resource | Architectural role |
|---|---|
| RP2350 internal SRAM | Deterministic PIO/DMA queues, descriptors, hot ROM/RAM, mailbox state, device state, and response metadata |
| External PSRAM | Bulk writable backing state, traces, snapshots, images, and service buffers |
| External Flash | RP2350 firmware plus persistent ROM/test images |
| External or host storage | Optional disk images and workload assets outside the current-cycle critical path |

The design rule is:

> **Internal SRAM is the deterministic hot path; PSRAM is bulk backing/workspace; persistent storage remains asynchronous.**

The current Pi86 HAT holds V30 `READY` high. Therefore a current-cycle service must either meet a bounded deterministic deadline or remain unsupported and high-Z. PSRAM, storage, or other slow resources cannot silently become unbounded direct responders.

See [`docs/adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](docs/adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

## Machine services and compatibility profiles

Memory-space and I/O-space transactions can be routed to software-defined machine services such as:

- 8259A-compatible interrupt control;
- 8253/8254-class timer behavior;
- diagnostic I/O and companion mailbox services;
- RAM/ROM regions;
- optional BIOS, storage, display, keyboard, or other PC-class services.

These services are modular capabilities rather than a mandatory linear implementation sequence.

A PC-class configuration is treated as an **optional compatibility profile**. It can exercise realistic BIOS, interrupt, timer, storage, and operating-system workloads without defining the core architecture. Other profiles may be intentionally smaller or more instrumented for diagnostics and experiments.

## Observation, control, and experiment plane

The project exposes the physical V30 to host-side tools through a machine-readable boundary designed for conventional automation first and AI-assisted workflows second.

```text
Physical V30
    |
Deterministic PIO / DMA bus plane
    |
+----------------+----------------+----------------+
|                |                |                |
Machine state    Observation      Control          Experiment
services         plane            plane            plane
|                |                |                |
+----------------+----------------+----------------+
                 |
        provider-neutral host API
                 |
       tools / scripts / AI agents
```

### Observation

Retained structured evidence may describe:

- reset and clock epochs;
- memory and I/O cycles;
- interrupt and two-cycle INTA activity;
- response class, hit/miss state, and deadlines;
- DMA/FIFO completion state;
- unqualified-drive prevention;
- machine/device state transitions;
- terminal electrical or persistent-runtime state.

Raw physical evidence must remain distinguishable from decoded or AI-derived interpretation.

### Control

Host control operations are bounded and explicit, for example:

- reset/run/validation stop;
- supported clock configuration;
- ROM or test-image selection;
- trace filters and triggers;
- supported IRQ or companion-request injection;
- safe machine/device-state queries;
- explicitly permitted test-memory or mailbox access outside unsafe current-cycle paths.

Every operation must define ownership, preconditions, failure behavior, and whether it is valid only for a bounded validation run or also for a persistent runtime.

### Experiment

A host-side experiment should be able to:

1. state a falsifiable hypothesis;
2. configure a bounded physical test;
3. execute it on the real V30;
4. retain machine-readable evidence with revision and configuration identity;
5. evaluate explicit invariants and PASS/FAIL criteria;
6. compare runs or operating points;
7. provide structured results to scripts or AI-assisted analysis.

AI may help form hypotheses, interpret failures, compare evidence, or select the next bounded experiment. It does not alter the acceptance rule after seeing the result.

See [Issue #50](https://github.com/cctsao1008/pi86-rp2350/issues/50) for the architecture tracker.

## Host bridge

A provider-neutral host bridge connects modern software to native code running on the physical V30 without placing the host in the realtime bus path.

The host-visible transport and the physical evidence path remain logically separate:

```text
application / AI request
        |
provider-neutral host protocol
        |
RP2350 publication / companion service
        |
physical V30 execution
        |
reply

physical bus evidence
        |
retained trace / structured validation result
        |
independent host verification
```

The V30-visible ABI uses ordinary machine concepts such as I/O ports, memory, software interrupts, hardware interrupts, and mailbox state. Provider-specific identities remain host-side concerns.

See [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md) and [`docs/companion_service_abi.md`](docs/companion_service_abi.md).

## Hardware contract

The hardware baseline is intentionally conservative:

- **Host board:** Waveshare RP2350-PiZero
- **MCU / programmable chipset:** Raspberry Pi RP2350B
- **CPU:** physical NEC V30 `D70116C-8` / `uPD70116C-8`
- **CPU interface:** original Homebrew8088 Pi86 V20/V30 HAT
- **Mechanical interface:** Raspberry Pi-compatible physical 40-pin header
- **HAT policy:** retain the existing validated hardware unless a demonstrated architectural blocker requires reconsideration
- **Onboard Flash:** 16 MB
- **External RAM direction:** APS6404L-class PSRAM as bulk backing/workspace
- **Host interface:** native USB for console, evidence, control, or bridge services as required by the runtime

The Raspberry Pi physical 40-pin header position is treated as the cross-platform hardware ABI. Canonical mapping and review rules are defined in [`docs/hardware_contract.md`](docs/hardware_contract.md).

> The installed `D70116C-8` is nominally a 5 V device. Operation on the original Pi86 HAT at 3.3 V is a project-specific empirical condition, not the nominal NEC operating specification.

Board reference: [Waveshare RP2350-PiZero Wiki](https://www.waveshare.com/wiki/RP2350-PiZero).

## Project lineage

`pi86-rp2350` builds directly on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor, observe address and control phases, and service memory and I/O transactions in software.

`pi86-rp2350` preserves the physical CPU and HAT interface while replacing the Linux/GPIO-driven control model with a bare-metal RP2350 architecture centered on deterministic PIO/DMA paths, explicit ownership, structured observability, bounded control, and physical evidence.

The project therefore explores not only how much of the chipset around an 8086-class processor can be expressed in an RP2350, but also how that physical machine can become a repeatable experimental target for modern automation and AI-assisted engineering.

## Roadmap

Current planning, priorities, and superseded historical gates are tracked in [Issue #39 - Physical V30 programmable-chipset and AI-operable platform](https://github.com/cctsao1008/pi86-rp2350/issues/39).

The README intentionally does not duplicate volatile issue status.

## Documentation

### Architecture

- [`docs/README.md`](docs/README.md) - documentation index and authority model
- [`docs/project_overview.md`](docs/project_overview.md) - project mission and research questions
- [`docs/architecture.md`](docs/architecture.md) - detailed system architecture
- [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md) - realtime and service ownership contract
- [`docs/adr/`](docs/adr/) - architecture decision records

### Host, AI, and machine interface

- [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md) - provider-neutral host/AI bridge
- [`docs/companion_service_abi.md`](docs/companion_service_abi.md) - host record and V30 mailbox ABI
- [`docs/ai_bridge_implementation_plan.md`](docs/ai_bridge_implementation_plan.md) - bridge implementation details

### Hardware

- [`docs/hardware_contract.md`](docs/hardware_contract.md) - canonical physical-interface contract
- [`docs/pin_mapping.md`](docs/pin_mapping.md) - physical signal mapping
- [`docs/pi86_hat_design_review.md`](docs/pi86_hat_design_review.md) - historical/current-HAT engineering review

### Native V30 software and workloads

- [`docs/native_bios_architecture.md`](docs/native_bios_architecture.md) - native V30 BIOS architecture
- [`docs/native_bios_diagnostic_console.md`](docs/native_bios_diagnostic_console.md) - diagnostic I/O contract
- [`docs/pc1c1_native_bios_platform.md`](docs/pc1c1_native_bios_platform.md) - native BIOS execution platform
- [`docs/elks_v30_fd1440_bringup.md`](docs/elks_v30_fd1440_bringup.md) - ELKS workload bring-up record

### Development and evidence

- [`docs/development/build_and_toolchain.md`](docs/development/build_and_toolchain.md) - build environment and commands
- [`docs/development/windows_physical_validation.md`](docs/development/windows_physical_validation.md) - Windows physical-validation host
- [`docs/bringup.md`](docs/bringup.md) - physical bring-up and operating procedure
- [`docs/validation/`](docs/validation/) - physical validation records and retained evidence

### Project story

- [`docs/story/`](docs/story/) - narrative material documenting the physical V30, memory, host interaction, and persistent runtime journey

## Acknowledgements

Special thanks to the original [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its creator for the V20/V30 HAT design, software architecture, and documentation that provided the foundation for this work.

`pi86-rp2350` preserves that physical interface while exploring a new implementation based on RP2350 PIO, DMA, deterministic software-defined chipset functions, structured physical evidence, and AI-operable host tooling.