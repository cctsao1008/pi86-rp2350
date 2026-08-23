# pi86-rp2350

**A physical NEC V30 with Raspberry Pi RP2350 PIO and DMA acting as its programmable companion chipset.**

`pi86-rp2350` preserves a real NEC V30 CPU and the original Pi86 V20/V30 HAT interface while replacing the Linux/GPIO polling model with a bare-metal RP2350 architecture built around deterministic PIO, DMA, internal SRAM, and explicit physical validation.

The active implementation and hardware validation target the **NEC V30**. V20 remains part of the original Pi86 compatibility lineage and reference model.

This is **not** an x86 emulator running on the RP2350.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 (V30) on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## Purpose

The RP2350 is treated as a **programmable chipset around a physical 8086-class processor**, not as a faster host that bit-bangs the V30 bus in software.

The central engineering question is:

> How far can an RP2350 act as a deterministic, software-defined companion chipset around a real NEC V30 - from reset-vector execution through ROM, RAM, peripherals, BIOS services, host interaction, and eventually a bootable PC-class system?

The project has two connected goals:

1. Build a progressively more capable physical V30 computer around an RP2350 companion chipset.
2. Build a verifiable bridge through which a modern host - including OpenAI Codex, ChatGPT, an OpenAI API client, or another program - can exchange messages and computation with native code running on that physical V30.

The simplest expression of the bridge goal is:

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

The greeting is only the first bounded transaction. The longer-term goal is fresh, verifiable computation and sustained interaction across the same physical boundary.

### A bridge between eras

The two endpoints were created roughly forty years apart and do not share a native vocabulary. The V30 understands machine code, interrupts, I/O ports, memory, and physical bus cycles. A modern host understands USB records, files, development tools, and optional AI services.

`pi86-rp2350` does not pretend that the V30 knows what AI is. It provides a translation boundary that lets each side remain native to its own era:

```text
Modern host / optional AI service
             |
     structured host protocol
             |
     RP2350 companion bridge
             |
  mailbox / interrupt / I/O / memory
             |
       Physical NEC V30
```

To V30 software, the RP2350 appears as a companion chipset and a set of defined machine services. To the host, the bridge presents structured requests, replies, and physical evidence. AI may interpret or generate host-side work, but it never becomes part of the current V30 bus cycle.

The purpose is not to erase the historical distance between the two systems. It is to give them a common language while preserving the physical processor, its native execution model, and the deterministic boundary between eras.

### A physical challenge for the modern agent

This project is also intended to challenge the modern engineering agent, not
merely provide it with tasks that are already easy to complete. The work crosses
schematics, datasheets, PIO timing, DMA ownership, x86 semantics, native BIOS
code, USB protocols, host tools, and physical validation. A plausible answer is
not enough: the real V30 and retained electrical evidence act as an independent
oracle.

An AI-assisted contribution should therefore be judged by whether it can:

1. state a falsifiable hardware or software hypothesis;
2. predict the evidence that would distinguish success from a convincing false positive;
3. produce bounded, reversible changes across multiple engineering domains;
4. preserve current-cycle timing and electrical ownership contracts;
5. interpret an unexpected physical failure without rewriting the acceptance rule;
6. improve the next experiment from retained evidence;
7. progress from scripted greetings toward fresh computation, sustained exchange, trace diagnosis, and native-code capsules.

The meaningful question is not only whether Codex can write code for the
project. It is whether a modern agent can reason across two computing eras,
submit its work to physical falsification, and help turn failure into a better
architecture.

## Architectural invariants

The implementation may evolve, but these rules define the project:

1. **The NEC V30 remains physical.** RP2350 firmware supplies the surrounding chipset functions; it does not replace the processor with an emulator.
2. **Current-cycle V30 timing belongs to PIO and DMA.** An Arm core must not be required to resolve an active physical response cycle.
3. **The Arm cores form a control and service plane.** They prepare immutable state, supervise bounded engines, handle exceptions, and provide asynchronous services outside the hardest timing path.
4. **Host transport is not a bus-timing mechanism.** USB, Python, storage, display, and AI clients operate outside the current V30 bus cycle.
5. **Application traffic and physical evidence remain separable.** A visible reply is not sufficient proof of correct physical execution.
6. **Unsupported cycles remain electrically safe.** The response engine must not drive unqualified cycles, and accepted runs must end with defined RESET, CLK, and AD-bus ownership.
7. **Target architecture and validated capability are stated separately.** A future memory or peripheral role is not treated as implemented merely because it appears in the system design.

The core timing hierarchy is:

```text
PIO / DMA       = hard real-time data path
Real-time core  = timing-sensitive control plane
Service core    = asynchronous system services
Host software   = policy, interaction, images, and validation
```

## System architecture

```text
             Codex / ChatGPT / API / other host program
                              |
                    provider-neutral Host Bridge
                              |
                     64-byte USB HID records
                              |
                              v
              +------------------------------------+
              |        Waveshare RP2350-PiZero     |
              |         Raspberry Pi RP2350B       |
              |                                    |
              |  service plane                     |
              |    USB / CDC / HID / debug         |
              |    image and peripheral services   |
              |                                    |
              |  control plane                     |
              |    immutable staging / supervision |
              |                                    |
              |  deterministic data plane          |
              |    PIO0  passive observation       |
              |    PIO1  qualified AD response     |
              |    PIO2  V30 clock / phase         |
              |    DMA   SRAM <-> PIO transport    |
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
                     +----------------------+

     CDC evidence <---- passive trace / terminal state ---- RP2350
```

The architectural contract is role-based. The current accepted PIO0/PIO1/PIO2 allocation is shown because it has been physically validated, but future implementations may move roles between state machines or PIO blocks without changing the ownership rules.

## PIO and DMA data plane

PIO is the principal architectural enabler. It provides tightly timed, hardware-like state machines that remain independent of Arm instruction latency and host activity.

### Passive observation

PIO0 observes physical V30 address, control, and data phases without taking ownership of the AD bus. DMA moves retained observations into SRAM so later software can decode evidence without perturbing the measured cycle.

### Qualified response

PIO1 owns deterministic ROM, memory, or mailbox response timing. A response is driven only when the current physical cycle matches a prequalified key or descriptor. PIO1 also controls the corresponding AD-bus `PINDIRS`, providing deterministic drive and release without DMA writes to SIO.

### Clock and phase authority

PIO2 owns the V30 clock and the phase relationship used by the accepted response engines. Separating clock authority from response and passive observation keeps the roles explicit and makes timing evidence easier to interpret.

### DMA transport

DMA transfers prepared keys, descriptors, response words, and passive observations between SRAM and PIO FIFOs. It performs bulk movement and deterministic refill; it does not turn an Arm core into a current-cycle responder.

The defining boundary is:

> **PIO/DMA answer the active bus cycle. Arm software prepares, supervises, and consumes state around it.**

## Dual-core control and service planes

The two RP2350 Arm cores are separated by responsibility so higher-level services cannot silently enter the V30-critical timing path.

- **Real-time role** - owns bus-epoch supervision, immutable publication, exceptional timing-sensitive coordination, and failure containment around the PIO/DMA engines.
- **Service role** - owns formatting, retained trace processing, USB-facing services, debugging, storage, display, and image management that can run asynchronously.

Cross-core communication uses bounded ownership transfer rather than shared mutable application state. Full queues must fail visibly and non-blockingly; a stalled service core must not change V30-visible behavior.

The numbered Core 0/Core 1 placement is intentionally provisional. Pico SDK USB constraints may require specific initialization or IRQ ownership, while the architectural contract remains real-time role, service role, and PIO/DMA current-cycle ownership. See [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md).

## Memory architecture

The V30 has a 20-bit physical address space (`00000h`-`FFFFFh`). The target companion-chip architecture maps qualified V30 memory transactions onto a hierarchy of deterministic and bulk storage resources.

| Resource | Architectural role |
|---|---|
| RP2350 internal SRAM | Deterministic PIO/DMA queues, descriptors, hot ROM/RAM, mailbox state, and virtual-device state |
| External PSRAM | Bulk V30 working memory, video memory, trace, snapshots, and large buffers |
| External Flash | RP2350 firmware plus persistent BIOS, ROM, and option/test images |
| MicroSD | Disk images and persistent PC storage |

The design rule is:

> **Internal SRAM is the deterministic hot path; PSRAM is bulk working memory; Flash and MicroSD provide persistence.**

The current physical foundation validates bounded internal-SRAM-backed ROM and RAM behavior. General arbitrary-address mapping, cache-miss handling, PSRAM-backed V30 memory, and complete PC memory semantics remain target capabilities rather than assumptions of this document.

## Peripheral architecture

Memory-space and I/O-space cycles are separate branches of the companion-chip architecture. Qualified I/O transactions may be routed to software-defined PC-class peripheral backends, including:

- **8259A-compatible PIC** - interrupt controller
- **8253/8254-class PIT** - programmable interval timer
- **8255-compatible PPI** - programmable parallel I/O controller
- **UART and diagnostic console** - early firmware and monitor interaction
- **Keyboard, display, and storage services** - system backends required by the BIOS and DOS path

These are parallel architectural branches, not a mandatory implementation sequence. Each backend must define its timing ownership, state model, interrupt behavior, and validation boundary before it becomes part of the accepted machine.

## AI Bridge architecture

The AI Bridge connects a modern host to native software running on the physical V30 without placing the host or an AI model in the realtime bus path.

```text
AI or host program
        |
        v
provider-specific adapter
        |
        v
provider-neutral Python Host Bridge
        |
        v
USB HID message plane
        |
        v
RP2350 immutable mailbox publication
        |
        v
PIO / DMA qualified V30 I/O response
        |
        v
native physical V30 program
```

The reverse path returns the V30 reply through the same bounded message ABI.

### Message plane

USB HID carries fixed 64-byte request and reply records. The ABI is provider-neutral: Codex is the first validated host adapter, but ChatGPT, an OpenAI API application, another agent, or a conventional program may use the same Host Bridge without changing the physical V30 contract.

### Evidence plane

USB CDC carries receive-only physical validation output. It reports the reset and ROM path, mailbox publication ordering, native V30 reads and writes, qualified response completion, deadline checks, and terminal electrical state.

> **HID carries the result. CDC explains why the result should be trusted.**

### Native mailbox

The bounded native mailbox uses explicit V30 I/O operations:

| Port | Direction | Role |
|---:|---|---|
| `00E0h` | RP2350 to V30 | STATUS / publication state |
| `00E4h` | RP2350 to V30 | host-to-V30 message data |
| `00E8h` | V30 to RP2350 | input-consumption witness |
| `00E2h` | V30 to RP2350 | V30-to-host reply data |
| `00E6h` | V30 to RP2350 | reply commit |

Publication is atomic from the V30's perspective: incomplete host records must never become mailbox-visible. A native reply is accepted only when its sequence, transport record, physical I/O evidence, and terminal state agree.

The bridge begins with a greeting, then progresses toward fresh challenge-response computation and repeated exchanges. The intended next proof is a host-generated challenge whose result must be calculated by native V30 code and independently verified by the host.

See [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md) for the canonical design and [`docs/ai_bridge_implementation_plan.md`](docs/ai_bridge_implementation_plan.md) for implementation gates.

## Hardware contract

### Validated baseline

- **Host board:** Waveshare RP2350-PiZero
- **MCU / programmable chipset:** Raspberry Pi RP2350B
- **CPU:** NEC V30 `D70116C-8` / `uPD70116C-8`
- **Installed CPU marking:** `1020VD002`
- **CPU interface:** original Homebrew8088 Pi86 V20/V30 HAT
- **Mechanical interface:** Raspberry Pi-compatible physical 40-pin header
- **Current HAT:** physically validated golden reference
- **Onboard Flash:** 16 MB
- **External RAM target:** APS6404L-class 8 MB PSRAM
- **Storage target:** onboard MicroSD
- **Display target:** onboard DVI using Pi86 virtual CGA memory
- **Host interface:** composite USB HID message transport and CDC evidence output

The Raspberry Pi physical 40-pin header position is treated as the cross-platform hardware ABI. Canonical mapping and review rules are defined in [`docs/hardware_contract.md`](docs/hardware_contract.md).

### READY and deterministic latency

The current HAT holds V30 `READY` high. It therefore cannot insert arbitrary wait states for ROM-cache misses, PSRAM latency, or slower peripheral service. Every current response path must meet a bounded deterministic deadline or leave the cycle unsupported and high-Z.

A consolidated V3.0 companion-chip board is intended to add controlled READY behavior, buffering, voltage-domain handling, and a separate realtime-control connector while retaining the legacy 40-pin data plane.

> The installed `D70116C-8` is nominally a 5 V device. Operation on the original Pi86 HAT at 3.3 V is a project-specific empirical condition, not the nominal NEC operating specification.

Board reference: [Waveshare RP2350-PiZero Wiki](https://www.waveshare.com/wiki/RP2350-PiZero).

## System capability goals

The companion-chip path grows by system capability rather than by clock frequency alone:

```text
RESET / instruction fetch
        |
        v
address-qualified ROM execution
        |
        v
general RAM service
        |
        v
I/O-space peripherals
   +---- 8259 PIC
   +---- 8253/8254 PIT
   +---- 8255 PPI
   +---- UART / keyboard / other devices
        |
        v
BIOS services
        |
        v
storage / display
        |
        v
DOS boot
```

The host-interaction path grows in parallel:

```text
fixed greeting
        |
        v
fresh challenge-response computation
        |
        v
ordered repeated exchanges
        |
        v
monitor and debugger services
        |
        v
AI-assisted interaction with the physical V30 system
```

The objective is not merely to maximize MHz. It is to preserve deterministic real-V30 execution while increasing the capability of the machine around it.

## Validation contract

This project is gate-based and hardware-validated. Acceptance is based on **CPU-visible behavior on the physical V30**, not merely on firmware completion or a convincing host-side string.

An accepted physical result should establish, as applicable:

- reset qualification and first post-reset address;
- cycle type, byte lanes, and current address;
- exact data visible to the V30;
- qualified PIO/DMA completion and deadline behavior;
- absence of unqualified drive commands;
- transport sequence and atomic publication;
- passive evidence independent of the application result;
- terminal RESET, CLK, and AD-bus ownership.

Failed or incomplete runs remain failures even when they contain an expected greeting. Raw evidence and machine-readable acceptance results are retained when they form part of an accepted gate.

Detailed implementation state belongs in [`docs/bringup.md`](docs/bringup.md), [`docs/validation/`](docs/validation/), and the project issue tracker. This README defines the architecture and goals rather than serving as a chronological status report.

## Capability boundary

Physical evidence already establishes the architectural foundation: reset-vector execution, PIO-direct qualified response, native internal-SRAM-backed ROM execution, bounded word and byte-lane RAM behavior, dual-core failure isolation, and a provider-neutral HID/CDC bridge used by Codex to exchange a message with the physical V30.

That foundation does **not** by itself claim:

- a complete arbitrary-address 1 MiB memory subsystem;
- general PSRAM-backed V30 RAM;
- arbitrary wait-state insertion on the current HAT;
- complete BIOS, PIC, PIT, PPI, display, or storage integration;
- DOS boot;
- an open-ended natural-language conversation running on the V30.

Representative evidence is indexed under [`docs/validation/`](docs/validation/), including the [Codex-initiated physical greeting](docs/validation/ai_b3_codex_initiated_greeting_validation.md) and [composite HID/CDC bridge](docs/validation/ai_b2_hid_composite_600khz_validation.md).

## Reference model

### CPU and bus

- **NEC V20/V30 User's Manual** - normative reference for V20/V30 pins, bus cycles, reset, interrupts, memory, I/O, timing, and electrical behavior
- **NEC 16-bit V-series Instruction Manual** - normative V20/V30 instruction-set reference
- **Intel 8088/8086 family documentation** - architectural and software-compatibility reference

At the software and system-architecture level, the NEC V20 corresponds broadly to the Intel 8088 class and the NEC V30 to the Intel 8086 class. They are not treated as electrically or pin-for-pin identical devices. NEC documentation is authoritative for the active physical V30 implementation.

### RP2350 platform

- **Raspberry Pi RP2350 Datasheet** - silicon, GPIO, PIO, DMA, QMI, SRAM, timing, and electrical behavior
- **Raspberry Pi Pico SDK documentation** - firmware API, USB constraints, and build-system reference
- [**Waveshare RP2350-PiZero Wiki**](https://www.waveshare.com/wiki/RP2350-PiZero) and schematic - board routing and onboard resources

### Tool behavior

- **NASM documentation** - V30-side assembly generation

## Project lineage

`pi86-rp2350` builds directly on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor, observe address and control phases, and service memory and I/O transactions in software. Its approximately 0.3 MHz operating point remains a historical comparison baseline.

`pi86-rp2350` preserves the physical CPU and HAT interface while replacing the Linux/GPIO-driven control model with a bare-metal RP2350 architecture centered on PIO, DMA, deterministic timing, explicit ownership, and physical evidence.

The objective is therefore not merely to port Pi86. It is to explore how much of the chipset traditionally surrounding an 8086-class processor can be expressed as a compact, programmable RP2350 companion system.

## Documentation map

### Architecture and goals

- [`docs/README.md`](docs/README.md) - complete documentation index and authority model
- [`docs/project_overview.md`](docs/project_overview.md) - project mission, research questions, and performance strategy
- [`docs/architecture.md`](docs/architecture.md) - detailed system architecture
- [`docs/ai_bridge_architecture.md`](docs/ai_bridge_architecture.md) - provider-neutral AI Bridge and native physical-V30 interaction
- [`docs/dual_core_partitioning.md`](docs/dual_core_partitioning.md) - realtime and service ownership contract

### Hardware

- [`docs/hardware_contract.md`](docs/hardware_contract.md) - canonical physical-interface contract
- [`docs/pin_mapping.md`](docs/pin_mapping.md) - physical signal mapping
- [`docs/pi86_hat_design_review.md`](docs/pi86_hat_design_review.md) - current HAT assessment and V3.0 direction

### Firmware and platform

- [`docs/native_bios_architecture.md`](docs/native_bios_architecture.md) - native V30 BIOS architecture
- [`docs/native_bios_diagnostic_console.md`](docs/native_bios_diagnostic_console.md) - early diagnostic I/O contract
- [`docs/pc1c1_native_bios_platform.md`](docs/pc1c1_native_bios_platform.md) - native BIOS execution platform

### Development and evidence

- [`docs/development/build_and_toolchain.md`](docs/development/build_and_toolchain.md) - build environment and commands
- [`docs/development/windows_physical_validation.md`](docs/development/windows_physical_validation.md) - Windows HID/CDC validation host
- [`docs/bringup.md`](docs/bringup.md) - active gate sequence and implementation state
- [`docs/validation/`](docs/validation/) - physical validation records and retained evidence
- [`docs/ai_bridge_implementation_plan.md`](docs/ai_bridge_implementation_plan.md) - AI Bridge implementation gates

## Acknowledgements

Special thanks to the original [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its creator for the V20/V30 HAT design, software architecture, and documentation that provided the foundation for this work.

`pi86-rp2350` preserves that physical interface while exploring a new implementation based on RP2350 PIO, DMA, deterministic software-defined chipset functions, and verifiable interaction with a physical NEC V30.
