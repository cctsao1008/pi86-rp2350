# pi86-rp2350

**⚡ A physical NEC V30 with Raspberry Pi RP2350 PIO and DMA acting as a deterministic, programmable companion chipset.**

`pi86-rp2350` keeps a real NEC V30 CPU and the original Pi86 V20/V30 HAT, while replacing the Linux/GPIO polling model with a bare-metal RP2350 architecture based on PIO, DMA, and deterministic on-chip state.

This is **not an x86 emulator**. The NEC V30 executes native code; the RP2350 provides the programmable machine around it.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 (V30) on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## 🧠 Concept

The RP2350 is treated as a **software-defined companion chipset around a physical 8086-class processor**, not as a faster host that bit-bangs the V30 bus in software.

The project is built around four ideas:

- **Physical** — the NEC V30 remains the processor executing native code.
- **Cycle-aware** — bus timing, ownership, and response behavior are explicit.
- **Programmable** — memory, I/O, interrupts, runtime services, and compatibility behavior can be supplied by the RP2350.
- **AI-operable** — modern tools and AI agents can observe, configure, and analyze the machine through structured host-side interfaces without entering the realtime bus path.

## 🏗️ Architecture

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
              |  service / control plane           |
              |    host I/O / machine state        |
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

The key boundary is:

> **PIO/DMA and bounded on-chip state handle current-cycle V30 timing. Arm software and host software prepare, supervise, and consume state around it.**

The deterministic bus plane provides passive observation, qualified response, clock/phase control, and DMA transport. Internal SRAM holds deterministic hot state; PSRAM and persistent storage are treated as backing/workspace rather than unbounded current-cycle responders.

The host-facing side exposes three generic capabilities:

- **Observe** — machine-readable bus and machine state.
- **Control** — bounded machine operations outside the current-cycle path.
- **Experiment** — repeatable configuration, execution, comparison, and analysis on the real V30.

AI remains host-side. It can reason over structured state, request bounded operations, and analyze experiments, but it is not part of V30 bus timing.

See [`docs/architecture.md`](docs/architecture.md), [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md), [Issue #50](https://github.com/cctsao1008/pi86-rp2350/issues/50), and [`docs/companion_service_abi.md`](docs/companion_service_abi.md).

## 🔌 Hardware and workloads

The working hardware baseline is:

- **Host board:** Waveshare RP2350-PiZero
- **MCU / companion chipset:** Raspberry Pi RP2350B
- **CPU:** physical NEC V30 `D70116C-8` / `uPD70116C-8`
- **CPU interface:** original Homebrew8088 Pi86 V20/V30 HAT
- **Mechanical interface:** Raspberry Pi-compatible 40-pin header
- **Onboard Flash:** 16 MB
- **External RAM direction:** APS6404L-class PSRAM as bulk backing/workspace
- **Host interface:** native USB for console, control, bridge, and tooling services

The existing Pi86 HAT remains the hardware baseline unless a demonstrated architectural limitation requires reconsideration.

PC-class BIOS, interrupt/timer devices, storage, display, DOS, ELKS, diagnostic ROMs, and other native software are treated as optional compatibility profiles and increasingly complex workloads rather than the architectural endpoint.

Canonical signal mapping and interface rules are documented in [`docs/hardware_contract.md`](docs/hardware_contract.md) and [`docs/pin_mapping.md`](docs/pin_mapping.md).

> The installed `D70116C-8` is nominally a 5 V device. Operation on the original Pi86 HAT at 3.3 V is a project-specific empirical condition, not the nominal NEC operating specification.

Board reference: [Waveshare RP2350-PiZero Wiki](https://www.waveshare.com/wiki/RP2350-PiZero).

## 🧬 Project lineage

`pi86-rp2350` builds directly on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor and service memory and I/O transactions in software.

`pi86-rp2350` preserves that physical CPU/HAT concept while moving the timing-critical boundary into RP2350 PIO, DMA, and bounded on-chip state, then exposing the resulting machine to modern host tooling and AI-assisted experimentation.

## 📚 Documentation

- [`docs/README.md`](docs/README.md) — documentation index
- [`docs/architecture.md`](docs/architecture.md) — detailed system architecture
- [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md) — realtime/service ownership
- [`docs/adr/`](docs/adr/) — architecture decisions
- [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md) — host/AI bridge architecture
- [`docs/companion_service_abi.md`](docs/companion_service_abi.md) — host and V30 companion ABI
- [`docs/hardware_contract.md`](docs/hardware_contract.md) — physical interface contract
- [`docs/bringup.md`](docs/bringup.md) — bring-up and operating procedure
- [`docs/validation/`](docs/validation/) — physical validation records

## 🙏 Acknowledgements

Special thanks to the original [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its creator for the V20/V30 HAT design, software architecture, and documentation that provided the foundation for this work.

`pi86-rp2350` preserves that physical interface while exploring a new implementation based on RP2350 PIO, DMA, deterministic software-defined chipset functions, structured host interaction, and AI-operable experimentation.