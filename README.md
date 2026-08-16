# pi86-rp2350

`pi86-rp2350` is evolving the original Pi86 physical V20/V30 computer into an RP2350-based **V30 companion chip**: a programmable, deterministic chipset around a real NEC processor.

The goal is not to reproduce a Raspberry Pi 2/3 software stack on a faster board. The project preserves the physical NEC V20/V30 while moving clock generation, bus control, ROM/RAM, peripherals, storage, and debugging into a bare-metal RP2350 design. PIO and DMA own the deterministic data plane; the Arm cores provide supervision and higher-level services where the measured timing budget permits.

The central engineering question is:

> How far can an RP2350 act as a programmable chipset around a real NEC V20/V30, from reset-vector execution through a monitor, BIOS services, and eventually a bootable PC-class system?

PC1-B has answered the first performance question: the PIO-direct fixed-response path is validated on physical hardware from 0.300 through 8.000 MHz configured V30 clock. The active question is now whether that timing result can be converted into address-qualified ROM/RAM and peripheral service without losing deterministic behavior.

## Development Model

This project uses an evidence-driven, gate-based development model.

Each gate introduces one clearly bounded capability and must be validated on the real V30/RP2350 hardware before the next capability is added.

Key rules:

- Preserve the last known-good hardware/software baseline.
- Change one major assumption or capability at a time.
- Separate refactoring from new functional behavior; refactors require regression validation first.
- Define measurable acceptance criteria before implementation.
- Prefer CPU-semantic validation over host-side code-path completion.
- Treat real hardware behavior and target-specific documentation as higher-priority evidence than inferred behavior.
- Record invalidated assumptions and superseded diagnostics instead of silently rewriting history.
- Do not advance to the next gate until the current gate has passed on hardware.

Typical progression:

```text
baseline
-> isolated capability
-> hardware validation
-> evidence capture
-> regression-safe architecture
-> next capability
```

The current gate sequence is documented in [`docs/bringup.md`](docs/bringup.md).

The broader cross-project methodology is maintained in [`cctsao1008/technical-management-framework`](https://github.com/cctsao1008/technical-management-framework); this repository contains only the Pi86-RP2350-specific application of that methodology.

## Project status

Validated through **Gate 12 and PC1-B on physical NEC V30 hardware**. The active milestone is **PC1-C ROM execution**. Current development work is tracked in [GitHub Issues](https://github.com/cctsao1008/pi86-rp2350/issues).

### Validated functional chain

```text
RESET / fetch
-> memory read/write
-> byte lanes / odd-address access
-> I/O space
-> maskable interrupt entry
-> reusable PIC backend
-> programmable 8259A-compatible PIC
-> multi-IRQ fixed priority / ISR blocking
-> programmable PIT channel 0 / IRQ0 path
-> PIO-direct V30 instruction response
-> 0.300-8.000 MHz fixed-response frequency sweep
```

PC1-B proves that a pre-staged `EB FE` response can travel through DMA, the PIO1 TX FIFO, and PIO-controlled scattered AD pins/PINDIRS quickly enough for the V30 to execute it at every tested clock point. It does **not** yet claim that arbitrary address-to-data ROM or RAM lookup is sustainable at 8 MHz; that is the PC1-C boundary.

Detailed gate definitions, acceptance criteria, and validation history are maintained in [`docs/bringup.md`](docs/bringup.md) and [`docs/validation/`](docs/validation/). Raw hardware evidence is archived separately in the project Google Drive.

## Success criteria

The project tracks three independent success dimensions:

```text
Functional
  physical V30 -> memory -> I/O -> PIC/PIT -> BIOS -> DOS

Architectural
  deterministic critical path, no Linux scheduler dependency,
  hard-real-time bus service separated from slower peripherals

Performance
  fixed-response PIO-direct path validated through 8 MHz,
  next characterize address-qualified ROM/RAM service and
  preserve real V30-class operation rather than chase clock rate alone
```

The original Pi86 project's reported approximately 0.3 MHz operating point is treated as the historical comparison baseline, not as a target architecture limit.

## Locked hardware baseline

- Host: Waveshare RP2350-PiZero
- CPU: NEC V30 `D70116C-8` / `uPD70116C-8`
- Installed CPU marking: `1020VD002`
- CPU HAT: original Homebrew8088 Pi86 V20/V30 (8088/8086) HAT
- Mechanical interface: HAT plugs directly into the RP2350-PiZero 40-pin Raspberry Pi-compatible header
- HAT PCB redesign: **not planned**
- Original Pi86 HAT physical-header assignment: **preserved**
- Planned external RAM: APS6404L-class 8 MB PSRAM on the RP2350-PiZero PSRAM footprint
- Storage target: onboard MicroSD
- Display target: onboard DVI using Pi86 virtual CGA memory
- Debug target: native USB CDC

> The installed `D70116C-8` is nominally a 5 V part. The original HAT is marked for 3.3 V V20/V30 operation. Treat 3.3 V operation of this specific CPU as a project-specific empirical condition, not as the nominal NEC rating.

## Hardware-interface rule

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

See [`docs/hardware_contract.md`](docs/hardware_contract.md) for the canonical mapping and permanent review rules.

## Architecture direction: V30 companion chip

The RP2350 is treated as a modern programmable chipset, not as a faster Linux host running Pi86-style GPIO polling.

- **PIO0:** continuous V30 clock, passive ALE/address observation, and phase capture
- **PIO1:** direct scattered-AD data output and `PINDIRS` ownership during read response windows
- **DMA:** deterministic SRAM-to-PIO FIFO movement without DMA writes to SIO
- **Real-time core role:** address/control decode, cache/refill supervision, and exceptional bus work that cannot remain entirely in PIO/DMA
- **Service core role:** ROM/disk images, USB/debug/keyboard, display, storage, and other non-real-time services
- **Bring-up memory:** RP2350 internal SRAM
- **Full system memory:** external PSRAM backend

The original HAT uses a scattered Raspberry Pi physical-header mapping. PC1-B handles it by encoding each 16-bit V30 word into a GPIO0-27 bitmap, routing only AD pins to PIO1, and switching bus ownership with RP2350 `MOV PINDIRS`. Input synchronizer bypass was not required for the 0.300-8.000 MHz validation.

The next architecture boundary is address-qualified service. The current HAT holds V30 `READY` high, so the existing hardware cannot insert wait states for a ROM-cache or PSRAM miss. PC1-C must therefore measure the complete address-capture, lookup, and response loop honestly before the project claims general 8 MHz memory service.

See [`docs/project_overview.md`](docs/project_overview.md) for the full project mission, research questions, and performance strategy.

## Engineering knowledge and decision records

Important project knowledge is deliberately version-controlled rather than left only in chat history:

- [`docs/project_overview.md`](docs/project_overview.md) — mission, research question, success criteria, and performance strategy.
- [`docs/hardware_contract.md`](docs/hardware_contract.md) — canonical hardware-interface contract and source hierarchy.
- [`docs/pin_mapping.md`](docs/pin_mapping.md) — implementation-oriented pin map.
- [`docs/adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](docs/adr/0001-use-rpi-physical-pin-as-hardware-abi.md) — architectural decision explaining why physical header position is canonical.
- [`docs/adr/0002-adopt-v30-companion-chip-architecture.md`](docs/adr/0002-adopt-v30-companion-chip-architecture.md) — decision to adopt the companion-chip architecture and its validation boundaries.
- [`docs/retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](docs/retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md) — bring-up postmortem, root cause, superseded diagnostic paths and permanent corrective actions.
- [`docs/validation/gate11_multi_irq_priority_validation.md`](docs/validation/gate11_multi_irq_priority_validation.md) — physical Gate 11 multi-IRQ validation evidence and acceptance result.
- [`docs/validation/pc1b_pio_direct_frequency_sweep.md`](docs/validation/pc1b_pio_direct_frequency_sweep.md) — PC1-B physical 0.300-8.000 MHz PIO-direct validation result.
- [`docs/pc1b_pio_direct_frequency_sweep_20260817.md`](docs/pc1b_pio_direct_frequency_sweep_20260817.md) — dated PC1-B sweep evidence and interpretation.
- [`docs/pc1c_rom_execution_plan.md`](docs/pc1c_rom_execution_plan.md) — active transition from fixed responses to ROM execution.
- [GitHub Issue #14](https://github.com/cctsao1008/pi86-rp2350/issues/14) — Gate 4 debugging archaeology and root-cause resolution history.
- [GitHub Issue #41](https://github.com/cctsao1008/pi86-rp2350/issues/41) — Gate 11 multi-IRQ fixed-priority validation, closed PASS.

A key retrospective lesson is that signal identity must be proven before signal behavior is interpreted. Earlier diagnostics performed under an incorrect BCM-to-RP2350 translation are retained as history but are explicitly superseded where their signal interpretation depended on the wrong mapping.

## Dependency model

The Raspberry Pi Pico SDK and picotool host utility are repository-pinned dependencies.

- Pico SDK is tracked as the Git submodule `third_party/pico-sdk`, pinned to **2.3.0**, commit `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- picotool is tracked as the Git submodule `third_party/picotool`, pinned to **2.3.0**, commit `6f6458d792b93685a11423b244a585eaa99eafcf`.
- Pico SDK contains its own nested submodules, so dependency initialization must use `--recursive`.
- `scripts/bootstrap_tools.sh` builds the pinned picotool with libusb support into the gitignored `.tools/` tree.
- Normal builds do **not** require a `PICO_SDK_PATH` environment variable.
- An explicit CMake `-DPICO_SDK_PATH=...` remains available only as an intentional local override.

This makes a project commit resolve to exact SDK and picotool source commits and keeps historical builds reproducible.

## Repository layout

```text
.
├── .gitmodules
├── CMakeLists.txt
├── firmware/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── board/
│   │   └── rp2350_pizero.h
│   ├── memory/
│   ├── pic/
│   ├── pit/
│   └── v30/
│       └── v30_pins.h
├── tests/
│   ├── gpio_test/
│   ├── gate2_preflight/
│   ├── gate3_reset/
│   ├── gate4_memread/
│   ├── gate7_byte_lanes/
│   ├── gate8_io/
│   ├── gate9_interrupt/
│   ├── gate9r_pic/
│   ├── gate10_8259a/
│   ├── gate11_pic_priority/
│   ├── gate11_irq_priority/
│   ├── gate12_pit_core/
│   ├── performance_characterization_1/
│   └── performance_characterization_1_extended/
├── docs/
│   ├── project_overview.md
│   ├── architecture.md
│   ├── hardware.md
│   ├── hardware_contract.md
│   ├── pin_mapping.md
│   ├── bringup.md
│   ├── bringup_gate11.md
│   ├── bringup_gate12.md
│   ├── pc1b_pio_direct_frequency_sweep_20260817.md
│   ├── pc1c_rom_execution_plan.md
│   ├── toolchain.md
│   ├── validation/
│   ├── adr/
│   │   ├── 0001-use-rpi-physical-pin-as-hardware-abi.md
│   │   └── 0002-adopt-v30-companion-chip-architecture.md
│   └── retrospectives/
│       └── 2026-08-rp2350-pi86-bringup-retrospective.md
├── scripts/
│   ├── bootstrap_tools.sh
│   ├── build.sh
│   └── build.ps1
├── third_party/
│   ├── pico-sdk/          # Git submodule, pinned to Pico SDK 2.3.0
│   └── picotool/          # Git submodule, pinned to picotool 2.3.0
└── references/
    └── README.md
```

## Clone

New clone:

```bash
git clone --recursive git@github.com:cctsao1008/pi86-rp2350.git
cd pi86-rp2350
```

Existing clone after pulling a commit that adds or changes dependencies:

```bash
git pull
git submodule update --init --recursive
```

Verify the pinned dependency:

```bash
git submodule status --recursive
```

## Build prerequisites

Host tools:

- Git
- CMake
- Arm GNU Toolchain supported by Pico SDK 2.3.0
- `pkg-config` and `libusb-1.0` development files for repository-local picotool
- Ninja is optional but recommended

The project uses the official Pico SDK board definition:

```text
waveshare_rp2350_pizero
```

That board definition selects the RP2350B package and the board's 16 MB flash configuration.

### Linux / WSL

Bootstrap the pinned host tool once:

```bash
./scripts/bootstrap_tools.sh
```

Then build normally:

```bash
./scripts/build.sh --clean
```

Build a single target:

```bash
./scripts/build.sh --target pi86_rp2350
./scripts/build.sh --target gpio_test
./scripts/build.sh --target gate9r_pic
./scripts/build.sh --target gate10_8259a
./scripts/build.sh --target gate11_pic_priority
./scripts/build.sh --target gate11_irq_priority
./scripts/build.sh --target gate12_pit_core
./scripts/build.sh --target pc1b_pio_direct_post_reset_epoch_sweep
```

### PowerShell

```powershell
.\scripts\build.ps1 -Clean
```

Build a single target:

```powershell
.\scripts\build.ps1 -Target pi86_rp2350
.\scripts\build.ps1 -Target gpio_test
.\scripts\build.ps1 -Target pc1b_pio_direct_post_reset_epoch_sweep
```

### Manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The root `CMakeLists.txt` defaults to the repository-pinned Pico SDK submodule and `PICO_BOARD=waveshare_rp2350_pizero`. The scripted Linux/WSL build additionally selects the repository-local picotool CMake package generated by `scripts/bootstrap_tools.sh`.

## Bring-up gates

See [`docs/bringup.md`](docs/bringup.md).

The first critical V30 milestone was:

```text
RESET -> first bus fetch -> 0xFFFF0
```

That milestone and the subsequent memory, I/O-space, interrupt, PIC, PIT, and PC1-B PIO-direct performance gates are validated on physical hardware. The next milestone is PC1-C0: execute an address-qualified ROM image beginning with a far jump from `FFFF0` to `F0000`, then reach a CPU-visible checkpoint rather than merely completing a host-side code path.

## Source and documentation policy

- GitHub contains source code, build files, pinned source dependencies, hardware-interface documentation, ADRs, retrospectives, issues, and version history.
- Google Drive is the evidence vault for original manuals/datasheets, hardware photos, bring-up logs, scope captures, benchmarks, long-form reports, and DOS boot evidence.
- NEC documentation is the normative source for V30 electrical and bus timing requirements.
- Original Pi86 source/HAT behavior is the compatibility reference.
- ArduinoX86 is a related implementation reference for physical x86/V20/V30 bus-control and validation concepts.

## License

No project license has been selected yet. Upstream Pi86 licensing and derivative-code obligations must be reviewed before Pi86 source is imported or redistributed here.
