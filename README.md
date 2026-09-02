# pi86-rp2350

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**
>
> **Host-Managed Bare-Metal Physical Processor Runtime**  
> *A modern remote-processor runtime for a vintage physical CPU.*

The physical processor is not emulated. An Intel 8086 or NEC V30 executes native x86-class machine code and owns its registers, control flow, interrupts, faults, and results. A modern Host loads and supervises that work. The RP2350 connects the two worlds by owning the physical bus and the shared resources around the processor.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## 🧠 Core idea

`pi86-rp2350` asks whether a real vintage CPU can become a physical processor that a modern Host can load, communicate with, supervise, and restart without rebuilding a traditional PC around it.

The physical processor knows only its native instruction set, interrupts, and physical bus. The Host provides loading, communication, files, supervision, and recovery. The RP2350 owns shared resources and the electrical bus discipline between them.

This changes the role of the processor from the center of a reconstructed vintage computer into a **bare-metal physical execution target inside a modern runtime**.

## 📖 Origin

The project began as hardware bring-up: connect the original Pi86 V20/V30 HAT to an RP2350-PiZero and determine whether a real NEC V30 could reliably leave RESET, fetch its first instruction, and execute native code.

The processor then progressed from reset fetch to memory access, Host communication, interrupt-driven liveness, and persistent runtime ownership. That shifted the architecture from reconstructing a fixed PC toward using the physical processor as a reusable execution engine.

The Intel 8086 later entered the same runtime, extending the architecture from one processor implementation to the 8086/V30 class.

## ⚙️ Runtime architecture

```text
Host
= runtime controller
  load / run / stdio / files / status / timeout / restart
             |
             | USB control, data, and observation
             v
RP2350
= companion resource and bus controller
  memory / storage / mailbox / interrupt / clock / reset / PIO / DMA
             |
             | physical 8086-class multiplexed bus
             v
Intel 8086 / NEC V30
= bare-metal remote physical processor
  native workload execution
```

The responsibility split is:

> **The Host manages the runtime. The RP2350 owns shared resources and the physical bus. The real Intel 8086 or NEC V30 executes bare-metal native workloads.**

Operationally:

```text
load -> run -> communicate -> observe -> exit / fault / timeout -> restart
```

BIOS, DOS, ELKS, and PC-compatible devices can be loaded as workloads or experiments, but the runtime itself is organized around direct physical-processor execution rather than a BIOS/DOS-first machine model.

## 🖥️ Host runtime

The reference Python runtime and shell are named **RP86**. `RP86` is processor-neutral: Intel 8086 and NEC V30 are explicit physical-processor profiles, while **RPBridge** names the CDC/HID and local-broker transport layer.

The RP86 runtime provides:

- workload loading, launch, stop, and restart;
- stdin/stdout and mailbox communication;
- file operations on RP2350-owned FAT volumes;
- processor-visible memory inspection and transfer;
- liveness, status, `top`, trace, timeout, and fault reporting.

The single Host runtime entry point is:

```text
tools/rp86.py
```

Python is the reference client for the Host Protocol; the protocol boundary is language-independent.

## 💾 Resource model

The RP2350 is the single low-level resource owner. Host and physical processor share content through it rather than directly sharing controllers or filesystem metadata.

| Resource | Runtime role |
|---|---|
| RP2350 Internal SRAM | firmware/realtime state, workload images, processor-visible RAM, and Host/processor shared memory |
| External PSRAM | optional capacity tier for larger workloads, bulk shared memory, snapshots, and cache/refill backing |
| External NOR Flash | firmware region plus shared `flash:` FAT volume |
| SD Card | optional removable `sd:` FAT volume |

Example shared paths:

```text
flash:/hello.bin
flash:/output.txt
sd:/datasets/input.dat
sd:/traces/run001.log
```

The physical processor sees assigned memory and runtime services; USB, PSRAM, NOR Flash, SD, FAT, PIO, and DMA remain RP2350-owned resources.

## ⏱️ Physical timing boundary

The original Pi86 HAT keeps the processor `READY` input asserted. The runtime supports two clock policies.

### FREE_RUNNING

The measured processor clock runs continuously while PIO/DMA and prepared state satisfy bus timing.

### CLOCK_STEPPED

The RP2350 issues one complete clock pulse at a time and may remain at `CLK=LOW` between pulses while servicing general Internal-SRAM memory or I/O.

The mode boundary allows the runtime to separate the electrical timing of a processor bus cycle from slower Host, filesystem, storage, or control work.

A native `INT 60h` path can request cooperative switching between the two clock policies at a complete bus-cycle boundary.

## 🔌 Hardware baseline

- Waveshare RP2350-PiZero with Raspberry Pi RP2350B
- physical Intel `P8086-2` or NEC V30 `D70116C-8` / `uPD70116C-8`
- original Homebrew8088 Pi86 V20/V30 HAT
- Raspberry Pi-compatible 40-pin physical interface
- 16 MB External NOR Flash
- optional External PSRAM footprint/device for capacity expansion
- native USB HID/CDC Host interface
- optional SD Card

The supported processors are nominally 5 V devices. Operation on the original Pi86 HAT at 3.3 V is a project-specific empirical operating condition rather than the nominal Intel or NEC specification.

## Intel 8086 and NEC V30

The runtime supports both physical NEC V30 and Intel 8086 processors on the same Pi86 HAT interface.

Neither processor provides CPUID, so the Host can identify the installed processor through the historical behavior difference of native `AAD 16` execution. The canonical `hello.bin` workload uses the same distinction and prints either:

```text
HELLO INTEL 8086
```

or:

```text
HELLO NEC V30
```

The workload lifecycle uses the same Host-controlled model for both processors:

```text
load → run → status → stop → restart
```

The runtime also exposes native examples such as interrupt-driven heartbeat, calculator execution, Host-loaded workloads, and shared-memory mailbox transformation. Detailed physical evidence is retained under [`docs/validation/`](docs/validation/).

## 📚 Documentation

- [`docs/architecture.md`](docs/architecture.md) — canonical system architecture
- [`docs/host_runtime_architecture.md`](docs/host_runtime_architecture.md) — detailed runtime and resource contract
- [`docs/host_runtime_shell.md`](docs/host_runtime_shell.md) — Host shell command model
- [`docs/memory_architecture.md`](docs/memory_architecture.md) — memory and shared-storage ownership
- [`docs/processor_memory_map.md`](docs/processor_memory_map.md) — Intel 8086 / NEC V30 physical address map
- [`docs/host_protocol.md`](docs/host_protocol.md) — language-independent Host Protocol
- [`docs/hardware.md`](docs/hardware.md) — board resources, signal mapping, and physical interface contract
- [`docs/development/codex_physical_development_loop.md`](docs/development/codex_physical_development_loop.md) — AI-written code through physical processor evidence
- [`processor/`](processor/) — native runtime and workload source executed by the physical processor
- [`docs/validation/`](docs/validation/) — physical execution evidence
- [`docs/README.md`](docs/README.md) — documentation map

Architecture decisions are recorded in:

- [`ADR 0008`](docs/adr/0008-adopt-host-managed-bare-metal-processor-runtime.md)
- [`ADR 0009`](docs/adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md)

## 🙏 Lineage and acknowledgements

`pi86-rp2350` builds on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT. Pi86 established the physical-processor concept; this project moves bus timing into RP2350 PIO/DMA and turns the surrounding system into a modern Host-managed runtime.
