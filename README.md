# pi86-rp2350

Port of Pi86 to the Waveshare RP2350-PiZero using the original Pi86 V20/V30 HAT and a physical NEC V30 CPU.

## Project status

**Phase:** hardware bring-up

**Current milestone:** validate the RP2350-PiZero host, then drive RESET and verify the V30's first bus fetch at physical address `0xFFFF0`.

## Locked hardware baseline

- Host: Waveshare RP2350-PiZero
- CPU: NEC V30 `D70116C-8` / `uPD70116C-8`
- Installed CPU marking: `1020VD002`
- CPU HAT: original Homebrew8088 Pi86 V20/V30 (8088/8086) HAT
- Mechanical interface: HAT plugs directly into the RP2350-PiZero 40-pin Raspberry Pi-compatible header
- HAT PCB redesign: **not planned**
- Original Pi86 GPIO/HAT mapping: **preserved**
- Planned external RAM: APS6404L-class 8 MB PSRAM on the RP2350-PiZero PSRAM footprint
- Storage target: onboard MicroSD
- Display target: onboard DVI using Pi86 virtual CGA memory
- Debug target: native USB CDC

> The installed `D70116C-8` is nominally a 5 V part. The original HAT is marked for 3.3 V V20/V30 operation. Treat 3.3 V operation of this specific CPU as a project-specific empirical condition, not as the nominal NEC rating.

## Architecture direction

The project preserves Pi86 system behavior while replacing Raspberry Pi/Linux/WiringPi GPIO bit-banging with deterministic RP2350 firmware.

- **PIO:** V30 clock generation and timing synchronization
- **Core 0 / SIO:** GPIO snapshots, scattered HAT pin packing/unpacking, V30 bus service, memory/I/O transactions, interrupt acknowledge
- **Core 1:** storage, display, USB/debug/keyboard, and slower system services
- **Bring-up memory:** RP2350 internal SRAM
- **Full system memory:** external PSRAM backend

The original HAT uses a scattered Raspberry Pi GPIO mapping. Performance work will use direct SIO access, masks, and lookup tables rather than per-pin high-level GPIO calls.

## Repository layout

```text
.
├── CMakeLists.txt
├── firmware/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── board/
│   │   └── rp2350_pizero.h
│   └── v30/
│       └── v30_pins.h
├── tests/
│   └── gpio_test/
│       ├── CMakeLists.txt
│       └── gpio_test.c
├── docs/
│   ├── architecture.md
│   ├── hardware.md
│   ├── pin_mapping.md
│   └── bringup.md
└── references/
    └── README.md
```

## Build prerequisites

- Raspberry Pi Pico SDK **2.3.0 or newer**
- CMake
- Arm GNU Toolchain supported by the installed Pico SDK

Pico SDK 2.3.0 added the official board configuration:

```text
waveshare_rp2350_pizero
```

That definition selects the RP2350B package and the board's 16 MB flash configuration, so this project no longer uses `pico2` as a bootstrap substitute.

Set `PICO_SDK_PATH` to your Pico SDK checkout before configuring.

Example:

```bash
cmake -S . -B build -DPICO_BOARD=waveshare_rp2350_pizero
cmake --build build --parallel
```

The root `CMakeLists.txt` also defaults `PICO_BOARD` to `waveshare_rp2350_pizero`, so the shorter form is valid when no other board is selected:

```bash
cmake -S . -B build
cmake --build build --parallel
```

Expected initial outputs include:

```text
build/firmware/pi86_rp2350.uf2
build/tests/gpio_test/gpio_test.uf2
```

## Initial executables

### `pi86_rp2350`

Minimal firmware sanity target. It initializes USB stdio and reports the locked hardware baseline. It does **not** drive the V30 bus yet.

### `gpio_test`

Gate 1 test target. It drives GPIO0-GPIO27 one at a time for use with the temporary Raspberry Pi GPIO test board.

**Do not run `gpio_test` with the V30 HAT installed.**

## Bring-up gates

See [`docs/bringup.md`](docs/bringup.md).

The first critical V30 milestone is:

```text
RESET -> first bus fetch -> 0xFFFF0
```

## Source and documentation policy

- GitHub contains source code, build files, hardware-interface documentation, issues, and version history.
- Google Drive is used for proposal documents, original manuals/datasheets, hardware photos, bring-up logs, scope captures, benchmarks, and DOS boot evidence.
- NEC documentation is the normative source for V30 electrical and bus timing requirements.
- Original Pi86 source/HAT mapping is the compatibility reference.
- ArduinoX86 is a related implementation reference for physical x86/V20/V30 bus-control and validation concepts.

## License

No project license has been selected yet. Upstream Pi86 licensing and derivative-code obligations must be reviewed before Pi86 source is imported or redistributed here.
