# pi86-rp2350

**A real NEC V30, with RP2350 PIO and DMA acting as its programmable chipset.**

`pi86-rp2350` evolves the original Pi86 physical V20/V30 computer into an RP2350-based **V30 companion-chip architecture**.

The project preserves the **physical NEC V30 CPU** while moving clock generation, bus control, ROM/RAM service, interrupt/timer peripherals, storage, display, and debugging into a bare-metal RP2350 system. PIO and DMA implement the deterministic bus data path; the RP2350 processor cores provide address decoding, supervision, and higher-level services where the measured timing budget permits.

This is **not** an x86 emulator running on the RP2350.

```text
Physical NEC V30
       +
   RP2350 PIO
       +
      DMA
       =
deterministic programmable chipset
```

The central engineering question is:

> How far can an RP2350 act as a programmable chipset around a real NEC V20/V30 — from reset-vector execution through ROM, RAM, peripherals, BIOS services, and eventually a bootable PC-class system?

## Project state

| Area | State |
|---|---|
| Physical NEC V30 bring-up | Validated |
| Memory / I/O bus | Validated |
| Maskable interrupt entry | Validated |
| 8259A-compatible PIC | Validated |
| PIT channel 0 / IRQ0 path | Validated |
| PC1-B fixed-response PIO-direct path | Validated from 0.300 to 8.000 MHz |
| **PC1-C address-qualified ROM execution** | **Active** |
| General 8 MHz ROM/RAM service | Not yet validated |
| 8255-compatible PPI | Planned |
| BIOS / DOS boot | Planned |

PC1-B proves that a pre-staged V30 instruction response can travel through RP2350 SRAM, DMA, the PIO1 TX FIFO, and PIO-controlled scattered AD pins/PINDIRS quickly enough for the physical V30 to execute correctly at every tested clock point.

It does **not** yet claim that arbitrary address-to-data ROM or RAM lookup is sustainable at 8 MHz. That is the PC1-C boundary.

Development is gate-based and hardware-validated. Acceptance is based on **CPU-visible behavior on the physical V30**, not merely completion of a host-side code path. See [`docs/bringup.md`](docs/bringup.md) and [`docs/validation/`](docs/validation/).

## Architecture

The RP2350 is treated as a programmable chipset, not as a faster Linux host running Pi86-style GPIO polling.

### Why PIO matters

RP2350 PIO is the key architectural enabler of this project.

Unlike software-driven GPIO polling, PIO state machines can execute tightly timed I/O sequences independently of the Arm cores. This allows the V30 bus-critical path to remain deterministic while address decoding, supervision, storage, debugging, and other higher-level services run outside that critical path.

In this design:

- **PIO0** generates and observes timing-critical bus phases
- **PIO1** owns direct V30 AD-bus response timing
- **DMA** feeds PIO without placing the Arm cores on the critical data path
- **Arm cores** supervise, decode, refill, and service work that cannot remain entirely in the deterministic PIO/DMA path

**PIO provides hardware-like timing with software-defined behavior.** That hardware/software boundary is what makes the RP2350 useful here as a programmable chipset rather than merely as an MCU performing GPIO control.

```text
                     +----------------------+
                     |     Physical V30     |
                     |    NEC D70116C-8     |
                     +----------+-----------+
                                |
                         V30 system bus
                                |
                    Original Pi86 V20/V30 HAT
                                |
                                v
              +------------------------------------+
              |        Waveshare RP2350-PiZero     |
              |              RP2350B               |
              |                                    |
              |  PIO0  clock / passive observe     |
              |  PIO1  AD bus response             |
              |  DMA   deterministic transfers     |
              |                                    |
              |  real-time core   decode/service   |
              |  service core     USB/storage/etc  |
              +------+-----------+----------+------+
                     |           |          |
                     v           v          v
                  SRAM        PSRAM       Flash
                     |           |          |
                fast path     V30 RAM      BIOS
                PIO/DMA       video        ROM
                queues        trace        firmware
```

Current partitioning:

- **PIO0** — continuous V30 clock, passive ALE/address observation, and phase capture
- **PIO1** — direct scattered-AD data output and bus-direction ownership during read-response windows
- **DMA** — deterministic SRAM-to-PIO FIFO data movement
- **Real-time core** — address/control decode, cache/refill supervision, and exceptional bus work
- **Service core** — ROM/disk images, USB/debug/keyboard, display, storage, and other non-real-time services

The current HAT holds V30 `READY` high, so the present hardware cannot insert arbitrary wait states for ROM-cache misses, PSRAM latency, or slower peripheral service. Deterministic response latency is therefore a first-class architectural constraint.

The Raspberry Pi **physical 40-pin header position** is treated as the cross-platform hardware ABI. See [`docs/hardware_contract.md`](docs/hardware_contract.md) for the canonical mapping and review rules.

## Hardware baseline

- **Host / chipset:** Waveshare RP2350-PiZero
- **MCU:** RP2350B
- **CPU:** NEC V30 `D70116C-8` / `uPD70116C-8`
- **Installed CPU marking:** `1020VD002`
- **CPU interface:** original Homebrew8088 Pi86 V20/V30 HAT
- **Mechanical interface:** Raspberry Pi-compatible physical 40-pin header
- **HAT redesign:** not planned
- **External RAM target:** APS6404L-class 8 MB PSRAM
- **Onboard Flash:** 16 MB
- **Storage target:** onboard MicroSD
- **Display target:** onboard DVI using Pi86 virtual CGA memory
- **Debug target:** native USB CDC

> The installed `D70116C-8` is nominally a 5 V device. Operation on the original Pi86 HAT at 3.3 V is treated as a project-specific empirical condition rather than the nominal NEC operating specification.

Board-level reference: [Waveshare RP2350-PiZero Wiki](https://www.waveshare.com/wiki/RP2350-PiZero).

## Memory and peripheral model

The project separates three memory roles:

> **Internal SRAM = deterministic fast path**  
> **External PSRAM = V30 bulk working memory**  
> **Flash = persistent firmware / BIOS / ROM storage**

| Resource | Primary role |
|---|---|
| RP2350 internal SRAM | PIO/DMA queues, bus state, hot memory, virtual-device state |
| External PSRAM | V30 RAM, video memory, trace, snapshots, large buffers |
| External Flash | RP2350 firmware, BIOS, option/test ROM images |
| MicroSD | PC storage and persistent disk images |

The V30 sees its normal 20-bit physical address space (`00000h`-`FFFFFh`). The RP2350 maps V30 memory and I/O transactions onto SRAM, PSRAM, Flash, or virtual-device backends.

Current peripheral state:

- **8259A-compatible PIC** — validated
- **8253/8254-class PIT path** — channel 0 / IRQ0 validated
- **8255-compatible PPI** — planned
- **UART / keyboard / display / storage services** — future integration as required by the BIOS/DOS path

These peripherals are parallel branches of the V30 I/O-space architecture rather than a strict implementation sequence.

## Toolchain

The project has two execution domains.

### RP2350 side

- C / C++
- Raspberry Pi Pico SDK
- Arm GNU Toolchain
- CMake
- picotool

### V30 side

- 16-bit x86/V30 assembly
- **NASM** for ROM, diagnostic, monitor, and BIOS-side test images

NASM-generated flat binaries can be embedded or mapped as V30 ROM images for hardware execution tests such as PC1-C.

## Reference model

References are grouped by scope rather than treated as one interchangeable source hierarchy.

### CPU and bus

- **NEC V20/V30 User's Manual** — normative source for the physical V30 hardware, pins, bus cycles, reset, interrupts, memory, and I/O behavior
- **NEC 16-bit V-series Instruction Manual** — normative V30 ISA reference, including addressing modes, execution behavior, and 8086/8088 correspondence
- **Intel 8086 family documentation** — architectural and software-compatibility reference for the underlying 8086-class model

Where NEC V30-specific behavior differs from Intel 8086 behavior, the NEC documentation takes precedence because the target CPU is a physical NEC V30.

### RP2350 platform

- **Raspberry Pi RP2350 Datasheet** — MCU architecture, GPIO, PIO, DMA, QMI, SRAM, timing, and electrical reference
- **Raspberry Pi Pico SDK documentation** — firmware API and build-system reference

### Board

- [**Waveshare RP2350-PiZero Wiki**](https://www.waveshare.com/wiki/RP2350-PiZero) and schematic — board routing, Flash, PSRAM footprint, DVI, MicroSD, USB, and 40-pin interface

### Tool behavior

- **NASM documentation** — normative reference for V30-side assembly generation

### Compatibility and lineage

- [**Homebrew8088 Pi86 project**](https://www.homebrew8088.com/home/raspberry-pi-second-project) — historical architecture, hardware behavior, BIOS/toolchain model, and the approximately 0.3 MHz comparison baseline
- Original Pi86 source and V20/V30 HAT — source-level and physical-interface compatibility reference
- Related physical x86/V20/V30 implementations — secondary engineering references

## Target progression

```text
RESET / instruction fetch
        ↓
address-qualified ROM execution
        ↓
RAM service
        ↓
I/O-space peripherals
   ├── 8259 PIC
   ├── 8253/8254 PIT
   ├── 8255 PPI
   └── UART / keyboard / other devices
        ↓
BIOS services
        ↓
storage / display
        ↓
DOS boot
```

Clock frequency alone is not the project goal. The objective is to preserve deterministic real-V30 operation while progressively increasing system capability.

## Documentation

- [`docs/project_overview.md`](docs/project_overview.md) — mission, architecture, research questions, and performance strategy
- [`docs/hardware_contract.md`](docs/hardware_contract.md) — canonical hardware-interface contract
- [`docs/bringup.md`](docs/bringup.md) — gate sequence and current validation state
- [`docs/validation/`](docs/validation/) — physical hardware validation records

Raw hardware evidence such as scope captures, photographs, logs, benchmarks, manuals/datasheets, and long-form experimental reports is archived separately.

## Build

The main RP2350 build dependencies are repository-pinned for reproducibility. Pico SDK and picotool are Git submodules; dependency initialization therefore uses `--recursive`.

### Prerequisites

- Git
- CMake
- Arm GNU Toolchain supported by the pinned Pico SDK
- `pkg-config`
- `libusb-1.0` development files
- **NASM 3.x** for V30/8086 ROM and diagnostic images
- Ninja optional but recommended

The project uses the Pico SDK board definition `waveshare_rp2350_pizero`.

### Clone

```bash
git clone --recursive git@github.com:cctsao1008/pi86-rp2350.git
cd pi86-rp2350
```

For an existing clone:

```bash
git pull
git submodule update --init --recursive
```

### Linux / WSL

```bash
./scripts/bootstrap_tools.sh
./scripts/build.sh --clean
```

Build the primary firmware:

```bash
./scripts/build.sh --target pi86_rp2350
```

Build the PC1-B validation target:

```bash
./scripts/build.sh --target pc1b_pio_direct_post_reset_epoch_sweep
```

### PowerShell

```powershell
.\scripts\build.ps1 -Clean
```

or:

```powershell
.\scripts\build.ps1 -Target pi86_rp2350
```

### Manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Normal builds use the repository-pinned Pico SDK and do not require a global `PICO_SDK_PATH`.

## Project provenance

`pi86-rp2350` builds directly on the [Homebrew8088 Pi86 project](https://www.homebrew8088.com/home/raspberry-pi-second-project) and its physical V20/V30 HAT.

The original Pi86 design used a Raspberry Pi to clock a physical 8088/8086/V20/V30-class processor, observe the control/address phases, and service memory and I/O transactions in software. Its approximately 0.3 MHz operating point is used here as a historical comparison baseline.

`pi86-rp2350` preserves the physical CPU and HAT interface while replacing the Linux/GPIO-driven control model with a bare-metal RP2350 architecture centered on PIO, DMA, deterministic timing, and explicit hardware validation.

The intent is therefore not merely to **port Pi86**. The longer-term objective is to explore whether the RP2350 can function as a compact, programmable implementation of much of the chipset traditionally surrounding an 8086-class processor.

GitHub stores source, architecture, build metadata, and validation summaries. Raw hardware evidence is archived separately.

## License

No project license has been selected yet. Upstream Pi86 licensing and derivative-code obligations must be reviewed before Pi86 source code is imported or redistributed by this repository.
