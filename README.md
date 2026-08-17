# pi86-rp2350

**A real NEC V30, with an RP2350 acting as its programmable chipset.**

`pi86-rp2350` evolves the original Pi86 physical V20/V30 computer into an RP2350-based **V30 companion-chip architecture**.

The project preserves the **physical NEC V30 CPU** while moving clock generation, bus control, ROM/RAM service, interrupt/timer peripherals, storage, display, and debugging into a bare-metal RP2350 system. PIO and DMA implement the deterministic bus data path; the RP2350 processor cores provide address decoding, supervision, and higher-level services where the measured timing budget permits.

This is **not** an x86 emulator running on the RP2350.

```text
Real NEC V30
     +
RP2350 programmable chipset
     +
PIO / DMA deterministic bus control
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

Detailed gate definitions and validation records are maintained in [`docs/bringup.md`](docs/bringup.md) and [`docs/validation/`](docs/validation/).

## Architecture direction: V30 companion chip

The RP2350 is treated as a programmable chipset, not as a faster Linux host running Pi86-style GPIO polling.

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
              +-----------------------------------+
              |        Waveshare RP2350-PiZero   |
              |              RP2350B              |
              |                                   |
              |  PIO0  clock / passive observe   |
              |  PIO1  AD bus response           |
              |  DMA   deterministic transfers   |
              |                                   |
              |  real-time core   decode/service |
              |  service core     USB/storage/etc|
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

See [`docs/project_overview.md`](docs/project_overview.md) for the full architecture and performance strategy.

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

## Hardware-interface contract

The Raspberry Pi **physical 40-pin header position** is the cross-platform hardware ABI.

Always translate:

```text
V30 signal
  -> Raspberry Pi physical header pin
  -> RP2350-PiZero board routing
  -> RP2350 GPIO
```

Do not conflate:

```text
WiringPi number != BCM GPIO != physical header pin != RP2350 GPIO
```

See [`docs/hardware_contract.md`](docs/hardware_contract.md) for the canonical mapping and [`docs/pin_mapping.md`](docs/pin_mapping.md) for the implementation-oriented pin map.

## Memory model

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

The V30 itself sees its normal 20-bit physical address space:

```text
00000h - FFFFFh
```

The RP2350 maps V30 memory and I/O transactions onto SRAM, PSRAM, Flash, or virtual-device backends.

## Peripheral model

The RP2350 progressively replaces traditional PC glue logic and peripheral controllers with software-defined implementations around the physical V30.

Current peripheral state:

- **8259A-compatible PIC** — validated
- **8253/8254-class PIT path** — channel 0 / IRQ0 validated
- **8255-compatible PPI** — planned
- **UART / keyboard / display / storage services** — future integration as required by the BIOS/DOS path

These peripherals are parallel branches of the V30 I/O-space architecture rather than a strict implementation sequence.

## Toolchain

The project has two execution domains and therefore two toolchain roles.

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

Original processor documentation is the primary source for CPU and bus behavior.

Reference priority:

1. **NEC V20/V30 User's Manual** — physical V30 hardware architecture, pin behavior, bus cycles, reset, interrupts, memory, and I/O
2. **NEC 16-bit V-series Instruction Manual** — V30 instruction set, addressing modes, execution behavior, and 8086/8088 correspondence
3. **Intel 8086 family documentation** — architectural and software-compatibility reference for the underlying 8086-class model
4. **Original Pi86 source and HAT behavior** — implementation and compatibility reference
5. **Related physical x86/V20/V30 implementations** — secondary engineering reference

Where NEC V30-specific behavior differs from the Intel 8086, the NEC documentation takes precedence for this project because the target CPU is a physical NEC V30.

For tool behavior, the NASM documentation is the normative reference for V30-side assembly generation.

## Engineering model

Development follows hardware-validated gates:

```text
known-good baseline
        -> isolated capability
        -> physical hardware validation
        -> evidence capture
        -> regression verification
        -> next capability
```

A gate does not advance merely because a host-side code path executes. Acceptance is based on **CPU-visible behavior on the physical V30**.

The complete gate sequence is maintained in [`docs/bringup.md`](docs/bringup.md).

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

Important project knowledge is version-controlled rather than retained only in development conversations.

- [`docs/project_overview.md`](docs/project_overview.md) — mission, research questions, success criteria, and performance strategy
- [`docs/hardware_contract.md`](docs/hardware_contract.md) — canonical hardware-interface contract and source hierarchy
- [`docs/pin_mapping.md`](docs/pin_mapping.md) — implementation-oriented pin map
- [`docs/bringup.md`](docs/bringup.md) — gate sequence and validation state
- [`docs/validation/`](docs/validation/) — physical hardware validation records
- [`docs/validation/pc1b_pio_direct_frequency_sweep.md`](docs/validation/pc1b_pio_direct_frequency_sweep.md) — PC1-B validation result
- [`docs/pc1c_rom_execution_plan.md`](docs/pc1c_rom_execution_plan.md) — active ROM-execution milestone
- [`docs/adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](docs/adr/0001-use-rpi-physical-pin-as-hardware-abi.md) — physical-header ABI decision
- [`docs/adr/0002-adopt-v30-companion-chip-architecture.md`](docs/adr/0002-adopt-v30-companion-chip-architecture.md) — companion-chip architecture decision
- [`docs/retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](docs/retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md) — bring-up retrospective

Raw hardware evidence such as scope captures, photographs, logs, benchmarks, and long-form experimental reports is archived separately.

## Dependencies

The main RP2350 build dependencies are repository-pinned for reproducibility.

- **Pico SDK 2.3.0** — `third_party/pico-sdk`, commit `98a542c1a62fb549ffb5d66a3e5892b06276b670`
- **picotool 2.3.0** — `third_party/picotool`, commit `6f6458d792b93685a11423b244a585eaa99eafcf`

Both are Git submodules. Pico SDK contains nested submodules, so dependency initialization should use `--recursive`.

## Quick start

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

### Build prerequisites

Host tools:

- Git
- CMake
- Arm GNU Toolchain supported by Pico SDK 2.3.0
- `pkg-config`
- `libusb-1.0` development files
- **NASM 3.x** for V30/8086 ROM and diagnostic images
- Ninja optional but recommended

The project uses the Pico SDK board definition:

```text
waveshare_rp2350_pizero
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

## Source and evidence policy

**GitHub** contains source code, build configuration, architecture documentation, hardware contracts, ADRs, validation summaries, issues, and version history.

The external **evidence archive** is used for original manuals/datasheets, hardware photographs, oscilloscope captures, raw bring-up logs, benchmarks, long-form reports, and eventual BIOS/DOS boot evidence.

## Project origin

`pi86-rp2350` is derived conceptually from the original Homebrew8088 Pi86 project, which demonstrated a physical NEC V20/V30 computer using a Raspberry Pi as the surrounding system.

This project preserves the original physical CPU and HAT interface while replacing the Linux-hosted control model with a bare-metal RP2350 architecture designed around PIO, DMA, deterministic timing, and explicit hardware validation.

The intent is therefore not merely to **port Pi86**. The longer-term objective is to explore whether the RP2350 can function as a compact, programmable implementation of much of the chipset traditionally surrounding an 8086-class processor.

## License

No project license has been selected yet. Upstream Pi86 licensing and derivative-code obligations must be reviewed before Pi86 source code is imported or redistributed by this repository.
