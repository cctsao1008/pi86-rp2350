# pi86-rp2350

Port of Pi86 to the Waveshare RP2350-PiZero using the original Pi86 V20/V30 HAT and a physical NEC V30 CPU.

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

**Phase:** reusable bus/memory architecture with byte-addressed memory validated; I/O-space bring-up next

**Validated functional chain:**

- Gate 0: RP2350-PiZero SDK / USB CDC baseline — PASS
- Gate 1: GPIO0-GPIO27 host validation — PASS
- Gate 2: V30 HAT electrical/preflight baseline — PASS
- Gate 3: RESET -> first physical fetch `0xFFFF0` — PASS
- Gate 4: aligned 16-bit memory read + prefetch-aware execution — PASS
- Gate 5: SRAM-backed executable ROM + far jump to physical `0xF0000` — PASS
- Gate 6: aligned RAM write/readback + CPU compare/branch verification — PASS
- Gate 6R: reusable `v30_bus` + byte-addressed memory refactor regression — PASS
- Gate 7: low/high byte lanes + odd-address 16-bit memory split/readback — PASS

The current validated memory scope is byte-addressed 8/16-bit memory read/write, including odd-address 16-bit accesses split by the V30 across HIGH/LOW byte lanes. General I/O, interrupts, PSRAM, BIOS/DOS compatibility and final clock-rate optimization remain separate future milestones.

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

## Architecture direction

The project preserves Pi86 system behavior while replacing Raspberry Pi/Linux/WiringPi GPIO bit-banging with deterministic RP2350 firmware.

- **PIO:** V30 clock generation and timing synchronization
- **Core 0 / SIO:** GPIO snapshots, scattered HAT pin packing/unpacking, V30 bus service, memory/I/O transactions, interrupt acknowledge
- **Core 1:** storage, display, USB/debug/keyboard, and slower system services
- **Bring-up memory:** RP2350 internal SRAM
- **Full system memory:** external PSRAM backend

The original HAT uses a scattered Raspberry Pi physical-header mapping. Performance work will use direct SIO access, masks, and lookup tables rather than per-pin high-level GPIO calls.

## Engineering knowledge and decision records

Important project knowledge is deliberately version-controlled rather than left only in chat history:

- [`docs/hardware_contract.md`](docs/hardware_contract.md) — canonical hardware-interface contract and source hierarchy.
- [`docs/pin_mapping.md`](docs/pin_mapping.md) — implementation-oriented pin map.
- [`docs/adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](docs/adr/0001-use-rpi-physical-pin-as-hardware-abi.md) — architectural decision explaining why physical header position is canonical.
- [`docs/retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](docs/retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md) — bring-up postmortem, root cause, superseded diagnostic paths and permanent corrective actions.
- [GitHub Issue #14](https://github.com/cctsao1008/pi86-rp2350/issues/14) — Gate 4 debugging archaeology and root-cause resolution history.

A key retrospective lesson is that signal identity must be proven before signal behavior is interpreted. Earlier diagnostics performed under an incorrect BCM-to-RP2350 translation are retained as history but are explicitly superseded where their signal interpretation depended on the wrong mapping.

## Dependency model

The Raspberry Pi Pico SDK is a repository dependency, not a machine-global prerequisite.

- Pico SDK is tracked as the Git submodule `third_party/pico-sdk`.
- The submodule is pinned to Pico SDK **2.3.0**, commit `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- Pico SDK contains its own nested submodules, so dependency initialization must use `--recursive`.
- Normal builds do **not** require a `PICO_SDK_PATH` environment variable.
- An explicit CMake `-DPICO_SDK_PATH=...` remains available only as an intentional local override.

This makes a project commit resolve to an exact SDK commit and keeps historical builds reproducible.

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
│   └── v30/
│       └── v30_pins.h
├── tests/
│   ├── gpio_test/
│   ├── gate2_preflight/
│   ├── gate3_reset/
│   ├── gate4_memread/
│   └── gate5_minrom/
├── docs/
│   ├── architecture.md
│   ├── hardware.md
│   ├── hardware_contract.md
│   ├── pin_mapping.md
│   ├── bringup.md
│   ├── toolchain.md
│   ├── adr/
│   │   └── 0001-use-rpi-physical-pin-as-hardware-abi.md
│   └── retrospectives/
│       └── 2026-08-rp2350-pi86-bringup-retrospective.md
├── scripts/
│   ├── build.sh
│   └── build.ps1
├── third_party/
│   └── pico-sdk/          # Git submodule, pinned to Pico SDK 2.3.0
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
- Ninja is optional but recommended

The project uses the official Pico SDK board definition:

```text
waveshare_rp2350_pizero
```

That board definition selects the RP2350B package and the board's 16 MB flash configuration.

### Linux / WSL

```bash
./scripts/build.sh --clean
```

Build a single target:

```bash
./scripts/build.sh --target pi86_rp2350
./scripts/build.sh --target gpio_test
```

### PowerShell

```powershell
.\scripts\build.ps1 -Clean
```

Build a single target:

```powershell
.\scripts\build.ps1 -Target pi86_rp2350
.\scripts\build.ps1 -Target gpio_test
```

### Manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The root `CMakeLists.txt` defaults to both the repository-pinned Pico SDK submodule and `PICO_BOARD=waveshare_rp2350_pizero`.

## Bring-up gates

See [`docs/bringup.md`](docs/bringup.md).

The first critical V30 milestone was:

```text
RESET -> first bus fetch -> 0xFFFF0
```

That milestone and subsequent memory-read, executable-ROM, RAM write/readback, and byte-lane/odd-address gates are now validated. Future gates should extend capability without invalidating the physical-header hardware contract.

## Source and documentation policy

- GitHub contains source code, build files, pinned source dependencies, hardware-interface documentation, ADRs, retrospectives, issues, and version history.
- Google Drive is the evidence vault for original manuals/datasheets, hardware photos, bring-up logs, scope captures, benchmarks, long-form reports, and DOS boot evidence.
- NEC documentation is the normative source for V30 electrical and bus timing requirements.
- Original Pi86 source/HAT behavior is the compatibility reference.
- ArduinoX86 is a related implementation reference for physical x86/V20/V30 bus-control and validation concepts.

## License

No project license has been selected yet. Upstream Pi86 licensing and derivative-code obligations must be reviewed before Pi86 source is imported or redistributed here.
