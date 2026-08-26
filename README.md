# pi86-rp2350

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**
>
> **Host-Managed Bare-Metal Physical Processor Runtime**
> *A modern remote-processor runtime for a vintage physical CPU.*

The physical processor is not emulated. An Intel 8086 or NEC V30 executes native
x86-class machine code and owns its registers, control flow, interrupts, faults,
and results. A modern Host
loads and supervises that work. The RP2350 connects the two worlds by owning the
physical bus and the shared resources around the processor.

<p align="center">
  <img src="docs/images/nec-v30-pi86-hat-rp2350-pizero.jpg" width="500" alt="Physical NEC V30 on the original Pi86 V20/V30 HAT connected to a Waveshare RP2350-PiZero">
</p>

<p align="center">
  <em>Physical NEC D70116C-8 on the original Pi86 V20/V30 HAT, connected to a Waveshare RP2350-PiZero.</em>
</p>

## 🧠 Why is this interesting?

Most retro-computing projects either recreate the computer that once surrounded
an old processor or emulate that processor in modern software. `pi86-rp2350`
asks a different question:

> **Can a real vintage CPU become a physical processor that a modern Host can
> load, communicate with, supervise, and restart—without first rebuilding a
> traditional PC around it?**

The V30 knows nothing about USB, Python, FAT filesystems, or AI. It only knows
its native instruction set, interrupts, and physical bus. The Host does not
pretend to be the V30 or execute instructions for it. Instead, the RP2350
bridges those two worlds: modern control and shared resources on one side, real
native execution on forty-year-old silicon on the other.

That changes the role of the processor. The V30 is no longer confined to being
the CPU of a reconstructed PC, and it is not reduced to a software model. It
becomes a bare-metal physical execution target inside a modern runtime.

> **This project is not only about making an old CPU boot again. It explores a
> new way for that CPU to remain useful, observable, and alive.**

## 📖 Story and motivation

This project began as a hardware bring-up: connect the original Pi86 V20/V30
HAT to an RP2350-PiZero and find out whether a real NEC V30 could reliably
leave RESET, fetch its first instruction, and execute native code.

Then the processor said hello. It learned to read and write memory. It
exchanged a physical message with a modern Host. Finally, instead of being
stopped after a test, it remained alive in `STI`/`HLT`, waking through real
interrupt acknowledge cycles, answering, and returning to sleep.

Those experiments changed the question. Reconstructing another fixed PC was no
longer the most interesting destination. The more compelling idea was to let
the physical V30 leave that historical machine behind while keeping the part
that matters: the real processor, executing its own native instructions.

That is the motivation for the runtime described here. A modern Host provides
loading, files, communication, supervision, and recovery. The RP2350 provides
the physical resources and bus discipline. The V30 is free to do the one thing
only it can do: execute as a real V30.

## ⚙️ The runtime

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
             | physical multiplexed V30 bus
             v
Intel 8086 / NEC V30
= bare-metal remote physical processor
  native workload execution
```

The responsibility split is fixed:

> **The Host manages the runtime. The RP2350 owns shared resources and the
> physical bus. The real Intel 8086 or NEC V30 executes bare-metal native workloads.**

Operationally:

```text
load -> run -> communicate -> observe -> exit / fault / timeout -> restart
```

This is not an x86 emulator, a PC/XT clone, or a BIOS/DOS-first computer. BIOS,
DOS, ELKS, and PC-compatible devices may still be loaded as experiments, but
they are not prerequisites and do not define the project.

## 🖥️ What the Host provides

The reference Python runtime is a small remote shell. Its stable command model
includes:

- workload loading, launch, stop, and restart;
- stdin/stdout and mailbox communication;
- file operations on RP2350-owned FAT volumes;
- V30-visible memory inspection and transfer;
- liveness, status, `top`, trace, timeout, and fault reporting.

Python is the first reference client, not the architecture. Other Host Protocol
implementations may be written in C or Rust and presented through CLI or Web
tools. Higher-level clients—including ChatGPT, Codex, and other agents—may use
those implementations without becoming part of the V30 runtime architecture.

The Host may disappear without becoming part of a current V30 bus cycle. A
workload can crash or stop responding; the runtime reports it, preserves
available evidence, and lets the user restart it.

## 💾 Resource model

The RP2350 is the single low-level owner. Host and V30 share content through it,
not raw controllers or filesystem metadata.

| Resource | Intended runtime role | Implementation / validation status |
|---|---|---|
| RP2350 Internal SRAM | firmware, PIO/DMA state, mailbox, cache/prepared windows, short traces | available; firmware use implemented; selected V30-visible paths physically validated in retained targets |
| External PSRAM | principal V30 execution memory and Host/V30 shared volatile workspace | SDK-backed detection/access framework implemented; arbitrary V30 execution not physically validated |
| External NOR Flash | first 4 MiB reserved for firmware; final 12 MiB is the shared `flash:` volume | FAT16 `RP-FLASH` mount, first-boot format, persistence, and media self-test physically validated; Host/processor file-service commands remain open |
| SD Card | optional removable `sd:` FAT volume | GPIO safe-state initialization implemented; card/FAT service not implemented |

Example shared paths:

```text
flash:/hello.bin
flash:/output.txt
sd:/datasets/input.dat
sd:/traces/run001.log
```

The V30 sees assigned memory and runtime services; it never directly owns USB,
PSRAM, NOR Flash, SD, FAT, PIO, or DMA controllers.

## ⏱️ Physical timing boundary

The original Pi86 HAT keeps V30 `READY` asserted. Timing-critical bus behavior
therefore remains in PIO/DMA and prepared RP2350 state. Host software, USB,
filesystem work, and arbitrary storage transactions do not answer an active
V30 bus cycle.

General PSRAM-backed arbitrary execution is the next major physical integration
gate. The architecture and Host shell are defined, but documentation does not
claim that this hardware path has already passed validation.

## 🔌 Hardware baseline

- Waveshare RP2350-PiZero with Raspberry Pi RP2350B
- physical Intel `P8086-2` or NEC V30 `D70116C-8` / `uPD70116C-8`
- original Homebrew8088 Pi86 V20/V30 HAT
- Raspberry Pi-compatible 40-pin physical interface
- 16 MB External NOR Flash
- External PSRAM footprint/device for the target runtime
- native USB HID/CDC Host interface
- optional SD Card

The supported processors are nominally 5 V devices. Operation on the original
Pi86 HAT at 3.3 V is a project-specific empirical condition, not the nominal
Intel or NEC specification.

### Intel 8086 support

On 2026-08-25, an Intel `P8086-2` replaced the NEC V30 in the same powered-down
HAT assembly and completed 55 interactive heartbeat exchanges with zero loss,
followed by a successful command exchange. This is strong evidence that the
runtime serves an Intel 8086-class physical processor. The retained run is an
interactive-attach observation; a complete Intel cold-boot transcript remains a
useful evidence improvement, not a prerequisite for processor support.

The Host accepts `--processor intel-8086` or `--processor nec-v30` because
neither processor provides CPUID. Processor identity is declared by the user
and recorded as Host metadata; it is not silently guessed by the runtime.

The canonical `hello.bin` workload adds an independent execution witness. It
uses the historical `AAD 16` behavior difference and prints either `HELLO
INTEL 8086` or `HELLO NEC V30` through the native diagnostic console. This
does not replace Host declaration; it lets the installed physical processor
demonstrate which instruction behavior it actually executed. The workload and
upload ABI are implemented, while arbitrary PSRAM-backed execution remains the
physical integration gate described above.

> **The V30 was not an accident. A real Intel 8086 entered the same
> runtime—and answered.**

See the retained
[`Intel 8086 interactive heartbeat observation`](docs/validation/intel_8086_interactive_heartbeat_1mhz_observation.md).

## 📚 Documentation

- [`docs/architecture.md`](docs/architecture.md) — canonical system architecture
- [`docs/host_runtime_architecture.md`](docs/host_runtime_architecture.md) — detailed runtime and resource contract
- [`docs/host_runtime_shell.md`](docs/host_runtime_shell.md) — Host shell command model
- [`docs/memory_architecture.md`](docs/memory_architecture.md) — memory and shared-storage ownership
- [`docs/host_protocol.md`](docs/host_protocol.md) — language-independent Host Protocol
- [`docs/hardware_contract.md`](docs/hardware_contract.md) — physical interface contract
- [`docs/validation/`](docs/validation/) — accepted physical evidence
- [`docs/archive/`](docs/archive/) — superseded plans and former project directions
- [`docs/README.md`](docs/README.md) — complete documentation map

The current architecture decisions are
[`ADR 0008`](docs/adr/0008-adopt-host-managed-bare-metal-processor-runtime.md)
and its processor-scope extension,
[`ADR 0009`](docs/adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md).

## 🙏 Lineage and acknowledgements

`pi86-rp2350` builds on the
[Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project)
and its physical V20/V30 HAT. Pi86 established the physical-processor concept;
this project moves bus timing into RP2350 PIO/DMA and turns the surrounding
system into a modern Host-managed runtime.
