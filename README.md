# pi86-rp2350

**A physical NEC V30 with Raspberry Pi RP2350 PIO and DMA acting as a deterministic, programmable companion chipset.**

`pi86-rp2350` keeps a real NEC V30 CPU and the original Pi86 V20/V30 HAT interface, while replacing the Linux/GPIO polling model with a bare-metal RP2350 architecture built around PIO, DMA, internal SRAM, explicit ownership, and host-side tooling.

This is **not an x86 emulator**. The NEC V30 executes native code; the RP2350 provides the programmable machine around it.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 (V30) on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## Purpose

The RP2350 is treated as a **software-defined companion chipset around a physical 8086-class processor**, not as a faster host that bit-bangs the V30 bus in software.

The central research question is:

> **How far can a modern programmable MCU turn a real legacy CPU into a cycle-aware, programmable, and AI-operable computing environment?**

The project concentrates on four properties:

- **Physical** — the NEC V30 remains the processor executing native code.
- **Cycle-aware** — bus timing, ownership, and response behavior are explicit.
- **Programmable** — memory, I/O, interrupts, runtime services, and compatibility behavior can be supplied by the RP2350.
- **AI-operable** — modern tools and AI agents can observe, configure, and analyze the machine through structured host-side interfaces without entering the realtime bus path.

## Architecture

```text
                 host tools / scripts / AI agents
                              |
                  provider-neutral host API
                              |
                observe / control / experiment
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
              |    machine state / supervision     |
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

The key boundary is simple:

> **PIO/DMA and bounded on-chip state handle current-cycle V30 timing. Arm software and host software prepare, supervise, and consume state around it.**

Exact PIO state-machine or Core 0/Core 1 placement may evolve; ownership of the realtime path is the architectural invariant.

## Deterministic bus plane

PIO and DMA are the principal architectural enablers.

- **Passive observation** captures physical V30 bus activity without taking ownership of the AD bus.
- **Qualified response** drives data only for explicitly supported physical cycles.
- **Clock and phase control** keep V30 timing under deterministic hardware-assisted control.
- **DMA transport** moves prepared response data and captured observations between SRAM and PIO without making an Arm core part of the active response cycle.

Memory follows the same separation:

| Resource | Role |
|---|---|
| RP2350 internal SRAM | Deterministic hot state, descriptors, ROM/RAM windows, mailbox/device state |
| External PSRAM | Bulk backing, traces, snapshots, images, and service workspace |
| External Flash | Firmware and persistent ROM/test images |
| External or host storage | Optional disk images and workload assets outside the current-cycle path |

The current Pi86 HAT holds V30 `READY` high, so slow backing resources are not assumed to be direct unbounded responders. Detailed timing policy is documented in [`docs/adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](docs/adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

## AI-operable interface

The AI connection is not an LLM running on the RP2350. AI remains host-side and uses the same interfaces available to conventional software.

```text
Physical V30
    |
Deterministic bus plane
    |
+-------------+-------------+-------------+
|             |             |             |
Observation   Control       Experiment
|             |             |             |
+-------------+-------------+-------------+
              |
      provider-neutral API
              |
     scripts / tools / AI
```

### Observe

Expose machine-readable physical and machine state: bus activity, memory/I/O transactions, interrupt activity, response class, timing metadata, and runtime state.

### Control

Provide bounded operations such as reset/run control, supported clock selection, ROM or test-image selection, trace configuration, IRQ/companion requests, and safe state queries.

### Experiment

Allow a host to configure a bounded test, execute it on the real V30, retain structured results, compare runs, and feed the same result to scripts or AI-assisted analysis.

AI may help form hypotheses, diagnose failures, and choose the next experiment. It does not participate in current-cycle GPIO timing.

See [Issue #50](https://github.com/cctsao1008/pi86-rp2350/issues/50), [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md), and [`docs/companion_service_abi.md`](docs/companion_service_abi.md).

## Hardware

The current hardware baseline is:

- **Host board:** Waveshare RP2350-PiZero
- **MCU / companion chipset:** Raspberry Pi RP2350B
- **CPU:** physical NEC V30 `D70116C-8` / `uPD70116C-8`
- **CPU interface:** original Homebrew8088 Pi86 V20/V30 HAT
- **Mechanical interface:** Raspberry Pi-compatible 40-pin header
- **Onboard Flash:** 16 MB
- **External RAM direction:** APS6404L-class PSRAM as bulk backing/workspace
- **Host interface:** native USB for console, control, bridge, and tooling services

The existing Pi86 HAT is the working hardware baseline. The project does not require a replacement board unless a demonstrated architectural limitation makes one necessary.

Canonical signal mapping and interface rules are documented in [`docs/hardware_contract.md`](docs/hardware_contract.md) and [`docs/pin_mapping.md`](docs/pin_mapping.md).

> The installed `D70116C-8` is nominally a 5 V device. Operation on the original Pi86 HAT at 3.3 V is a project-specific empirical condition, not the nominal NEC operating specification.

Board reference: [Waveshare RP2350-PiZero Wiki](https://www.waveshare.com/wiki/RP2350-PiZero).

## Compatibility and workloads

PC-class services are useful, but they are **profiles and workloads rather than the definition of the project**.

Examples include:

- 8259A-compatible interrupt control;
- 8253/8254-class timer behavior;
- BIOS and diagnostic ROMs;
- RAM/ROM machine profiles;
- storage, display, and keyboard services;
- DOS, ELKS, and other native V30 software.

These workloads are valuable because they exercise increasingly complex interactions between the physical CPU and the programmable chipset without forcing the project into a conventional PC/XT-clone roadmap.

## Project lineage

`pi86-rp2350` builds directly on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor and service memory and I/O transactions in software.

`pi86-rp2350` preserves that physical CPU/HAT concept while moving the timing-critical boundary into RP2350 PIO, DMA, and bounded on-chip state, then exposing the resulting machine to modern host tooling and AI-assisted experimentation.

## Roadmap

Current priorities and the active architecture roadmap are tracked in [Issue #39 — Physical V30 programmable-chipset and AI-operable platform](https://github.com/cctsao1008/pi86-rp2350/issues/39).

Detailed performance characterization is tracked by response class rather than by a single headline clock target. See [Issue #43](https://github.com/cctsao1008/pi86-rp2350/issues/43).

## Documentation

- [`docs/README.md`](docs/README.md) — documentation index
- [`docs/project_overview.md`](docs/project_overview.md) — mission and research questions
- [`docs/architecture.md`](docs/architecture.md) — detailed system architecture
- [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md) — realtime/service ownership
- [`docs/adr/`](docs/adr/) — architecture decisions
- [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md) — host/AI bridge architecture
- [`docs/companion_service_abi.md`](docs/companion_service_abi.md) — host and V30 companion ABI
- [`docs/hardware_contract.md`](docs/hardware_contract.md) — physical interface contract
- [`docs/bringup.md`](docs/bringup.md) — bring-up and operating procedure
- [`docs/validation/`](docs/validation/) — physical validation records
- [`docs/elks_v30_fd1440_bringup.md`](docs/elks_v30_fd1440_bringup.md) — ELKS workload bring-up

## Acknowledgements

Special thanks to the original [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its creator for the V20/V30 HAT design, software architecture, and documentation that provided the foundation for this work.

`pi86-rp2350` preserves that physical interface while exploring a new implementation based on RP2350 PIO, DMA, deterministic software-defined chipset functions, structured host interaction, and AI-operable experimentation.
