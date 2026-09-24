<p align="center">
  <img src="docs/images/pi86-mascot.svg" width="240" alt="pi86-rp2350 mascot">
</p>

<h1 align="center">pi86-rp2350</h1>

<p align="center">
  <strong>Host-Managed Bare-Metal Physical Processor Runtime</strong>
</p>

<p align="center">
  <strong>Real 8086-class silicon. Modern host control. No CPU emulation.</strong>
</p>

<p align="center">
  <em>Prepare in software. Serve in realtime. Execute in silicon.</em>
</p>

<p align="center">
  🖥️ Orchestrate &nbsp;·&nbsp; ⚡ Serve &nbsp;·&nbsp; 🧠 Execute &nbsp;·&nbsp; 🔬 Validate
</p>

`pi86-rp2350` is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors. The physical processor is not emulated: it executes native IA-16 machine code and owns its registers, control flow, interrupts, faults, and results. A modern Host loads and supervises that work, while the RP2350 connects the two worlds through a service plane and a hardware-paced realtime processor-bus data plane.

> **Vintage silicon. Modern runtime. Realtime boundaries stay local.**

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
Host control plane
  RP86 / CLI / Web / Remote
             |
             | USB Host Protocol
             v
RP2350 service / policy plane
  M33: lifecycle / memory ownership / storage / telemetry
             |
             | prepared bounded state
             v
RP2350 realtime data plane
  PIO + DMA: bus / memory / I/O / INTA / clock timing
             |
             | physical 8086-class multiplexed bus
             v
Intel 8086 / NEC V30
  native workload execution authority
```

The responsibility split is:

> **The Host orchestrates the runtime. RP2350 M33 manages services and policy. PIO/DMA owns bounded realtime processor interaction. The real Intel 8086 or NEC V30 executes the workload.**

A central rule is:

> **An active processor bus cycle must not depend synchronously on PC software, USB latency, filesystem work, or other unbounded software latency.**

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

The canonical Host runtime entry point is:

```text
host/apps/cli/rp86.py
```

The reusable Host runtime implementation lives under:

```text
host/rp86/
```

Python is the reference client for the Host Protocol; the protocol boundary is language-independent.

## 💾 Resource model

The RP2350 is the single low-level resource owner. Host and physical processor share content through it rather than directly sharing controllers or filesystem metadata.

| Resource | Runtime role |
|---|---|
| RP2350 Internal SRAM | firmware and realtime state, workload backing, processor-visible memory, and Host/processor shared-memory regions |
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

Logical memory ownership and realtime processor-bus service are separate concerns: a region may be assigned as processor-visible backing while its processor-specific no-wait realtime access path is still under validation.

USB, PSRAM, NOR Flash, SD, FAT, PIO, and DMA remain RP2350-owned resources.

## ⏱️ Physical timing boundary

The original Pi86 HAT keeps processor `READY` asserted, so the RP2350 cannot insert conventional wait states during an active processor cycle.

### Intel 8086

Canonical Intel 8086 general execution uses a **continuously running, vendor-compliant clock**. Every supported memory, I/O, and interrupt-acknowledge cycle must therefore complete through a bounded realtime path such as prepared PIO/DMA state.

`CLOCK_STEPPED` remains useful for bring-up, diagnostics, historical validation, and empirical experiments, but it is **not** an Intel-compliant general-execution mode. Existing Intel results obtained at 1 MHz or with stopped-clock execution remain valid empirical observations, not vendor-compliant operating points.

### NEC V30

The NEC V30 uses the same Host/RP2350 runtime architecture and Pi86 physical interface, but its clock-stop and minimum-frequency guarantees are tracked independently from the Intel 8086 contract.

> **Shared runtime architecture does not imply a shared silicon timing contract.**

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

## 🧩 Intel 8086 and NEC V30

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

## 🔬 Verification model

Software-side execution models are used to eliminate hypotheses before physical validation.

```text
structural / contract checks
          -> Host / firmware tests
          -> IA16 binary execution lab
          -> integration / realtime-path tests
          -> physical Intel 8086 / NEC V30
```

The IA16 Lab is a **binary execution microscope for production IA-16 artifacts**, not an RP86 emulator. Physical silicon remains the final evidence authority.

## 📚 Documentation

- [`docs/architecture/README.md`](docs/architecture/README.md) — canonical system architecture
- [`docs/architecture/repository_structure.md`](docs/architecture/repository_structure.md) — source-tree ownership model
- [`docs/architecture/host_runtime.md`](docs/architecture/host_runtime.md) — detailed runtime and resource contract
- [`docs/architecture/intel_8086_clock_contract.md`](docs/architecture/intel_8086_clock_contract.md) — Intel continuous-clock contract and implications
- [`docs/reference/host_runtime_shell.md`](docs/reference/host_runtime_shell.md) — Host shell command model
- [`docs/architecture/memory.md`](docs/architecture/memory.md) — memory and shared-storage ownership
- [`docs/reference/processor_memory_map.md`](docs/reference/processor_memory_map.md) — Intel 8086 / NEC V30 physical address map
- [`docs/reference/host_protocol.md`](docs/reference/host_protocol.md) — language-independent Host Protocol
- [`docs/architecture/hardware.md`](docs/architecture/hardware.md) — board resources, signal mapping, and physical interface contract
- [`docs/development/codex_physical_development_loop.md`](docs/development/codex_physical_development_loop.md) — AI-written code through physical processor evidence
- [`processor/`](processor/) — native runtime and workload source executed by the physical processor
- [`docs/validation/`](docs/validation/) — physical execution evidence
- [`docs/README.md`](docs/README.md) — documentation map

Architecture decisions include:

- [`ADR 0008`](docs/adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — runtime identity
- [`ADR 0009`](docs/adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md) — processor scope
- [`ADR 0010`](docs/adr/00010-adopt-free-running-and-clock-stepped-execution.md) — historical execution-clock modes
- [`ADR 0011`](docs/adr/0011-constrain-intel-8086-to-continuous-in-spec-clock.md) — Intel continuous in-spec clock requirement

## 🧭 Documentation principle

> **README explains the system. Issues explain the journey. Code proves the current state.**

README and durable documentation describe the processor-runtime architecture, contracts, rationale, usage, and evidence interpretation. GitHub Issues preserve experiments, bring-up paths, temporary constraints, design alternatives, and implementation journeys. Code, configuration, protocols, and tests remain the authoritative evidence of executable behavior.

## 🙏 Lineage and acknowledgements

`pi86-rp2350` builds on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT. Pi86 established the physical-processor concept; this project moves processor-facing realtime behavior into RP2350 PIO/DMA and turns the surrounding system into a modern Host-managed runtime.
