# pi86-rp2350

**A physical NEC V30 with Raspberry Pi RP2350 PIO and DMA acting as a deterministic, programmable machine platform.**

`pi86-rp2350` keeps a real NEC V30 CPU and the original Pi86 V20/V30 HAT, while replacing the Linux/GPIO polling model with a bare-metal RP2350 architecture based on PIO, DMA, and deterministic on-chip state.

This is **not an x86 emulator**. The NEC V30 executes native code; the RP2350 constructs, controls, and observes the machine around it.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 (V30) on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## Concept

The project has three actors:

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

The governing execution model is:

> **The RP2350 constructs the V30-visible machine before execution; the physical V30 then executes inside that prepared environment.**

The hard realtime boundary is:

> **PIO/DMA and bounded on-chip state own current-cycle V30 timing. Arm software, USB, storage, and host tools operate outside that active-cycle path.**

The V30 does not require a BIOS or operating system to form the core machine. BIOS, DOS, ELKS, PC-class devices, and other compatibility behavior remain optional workloads or profiles.

## Architecture

The minimum RP2350 firmware has six responsibilities:

- **V30 Bus Engine** - deterministic clock, capture, qualified response, write capture, and bus safety;
- **Machine Control** - RESET, clock, state, and fault/safe-state control;
- **Memory** - V30 Memory Map, backing resources, and deterministic preparation;
- **Workload / Reset Handoff** - prepare native code and transfer execution from the V30 reset state;
- **Host Interface** - USB HID command/response and USB CDC observation;
- **Persistent Storage** - External NOR Flash, filesystem, firmware/recovery data, and machine assets.

Current-cycle V30 response must not depend on USB, host latency, a filesystem operation, External NOR Flash access, or arbitrary External PSRAM latency.

See [`docs/architecture.md`](docs/architecture.md).

## Host interface

The project defines a small language-independent wire protocol rather than a mandatory host SDK:

```text
USB HID = structured command / response
USB CDC = log / diagnostic / observation
```

Python, C, Rust, PowerShell, Web applications, scripts, CLIs, AI agents, or other programs may use the same protocol. Repository tools are reference/sample clients, not required architecture layers.

See [`docs/host_protocol.md`](docs/host_protocol.md).

## Memory model

Canonical physical resources are:

- **RP2350 Internal SRAM** - firmware runtime and deterministic working state;
- **External NOR Flash** - 16 MB on the current RP2350-PiZero; persistent firmware, recovery capacity, filesystem, and assets;
- **External PSRAM** - target-machine bulk volatile backing/workspace;
- **SD Card** - optional removable bulk storage.

These physical resources are separate from the **V30 Memory Map**, which describes CPU-visible address semantics.

The minimum useful V30 Memory Map contains a Reset Handoff Region, Executable Region, Writable RAM Region, and defined behavior for unmapped addresses. It does not require the IBM PC conventional-memory/VGA/BIOS layout.

An Internal-SRAM + NOR-Flash configuration remains useful for bring-up and diagnostics. External PSRAM is part of the target machine configuration so bulk volatile state does not consume deterministic Internal SRAM.

See [`docs/memory_architecture.md`](docs/memory_architecture.md).

## Hardware baseline

The working physical baseline is:

- **Host board:** Waveshare RP2350-PiZero
- **MCU / machine platform:** Raspberry Pi RP2350B
- **CPU:** physical NEC V30 `D70116C-8` / `uPD70116C-8`
- **CPU interface:** original Homebrew8088 Pi86 V20/V30 HAT
- **Mechanical interface:** Raspberry Pi-compatible 40-pin header
- **External NOR Flash:** 16 MB
- **External PSRAM:** target-machine bulk volatile backing/workspace
- **Host interface:** native USB HID + CDC
- **SD Card:** optional

The existing Pi86 HAT remains the hardware baseline unless a demonstrated architectural limitation requires reconsideration.

The installed `D70116C-8` is nominally a 5 V device. Operation on the original Pi86 HAT at 3.3 V is a project-specific empirical condition, not the nominal NEC operating specification.

Canonical signal mapping and electrical rules are documented in [`docs/hardware_contract.md`](docs/hardware_contract.md) and [`docs/pin_mapping.md`](docs/pin_mapping.md).

## Optional compatibility and workloads

The repository contains validated and experimental work for native ROM execution, RAM, interrupts, PIC/PIT behavior, BIOS, host/V30 mailbox interaction, DOS/ELKS exploration, and other progressively complex workloads.

These remain useful engineering evidence and optional compatibility mechanisms. They do not define the minimum architecture.

Historical validation records are preserved as measured evidence even when later architecture decisions change their interpretation.

## Project lineage

`pi86-rp2350` builds directly on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor and service memory and I/O transactions in software.

`pi86-rp2350` preserves that physical CPU/HAT concept while moving the timing-critical boundary into RP2350 PIO, DMA, and bounded on-chip state.

## Documentation

- [`docs/README.md`](docs/README.md) - documentation index
- [`docs/architecture.md`](docs/architecture.md) - canonical system architecture
- [`docs/memory_architecture.md`](docs/memory_architecture.md) - memory and V30 Memory Map architecture
- [`docs/host_protocol.md`](docs/host_protocol.md) - Host/RP2350 HID/CDC protocol
- [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md) - realtime/service ownership
- [`docs/hardware_contract.md`](docs/hardware_contract.md) - physical interface contract
- [`docs/adr/`](docs/adr/) - architecture decisions
- [`docs/validation/`](docs/validation/) - physical validation records

Current machine-model decision: [`docs/adr/0007-adopt-host-constructed-v30-machine-model.md`](docs/adr/0007-adopt-host-constructed-v30-machine-model.md).

## Acknowledgements

Special thanks to the original [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its creator for the V20/V30 HAT design, software architecture, and documentation that provided the foundation for this work.
