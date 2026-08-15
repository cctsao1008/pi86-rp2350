# Toolchain and Build Setup

## Baseline

This project uses the official Pico SDK board definition:

```text
waveshare_rp2350_pizero
```

Pico SDK **2.3.0** is tracked as the Git submodule:

```text
third_party/pico-sdk
```

The repository pins the submodule to commit:

```text
98a542c1a62fb549ffb5d66a3e5892b06276b670
```

That is the Pico SDK `2.3.0` tag. SDK 2.3.0 includes the official Waveshare RP2350-PiZero board definition, including RP2350B package selection and the board's 16 MB flash configuration.

The SDK itself contains nested submodules such as TinyUSB, so always initialize dependencies recursively.

## Dependency policy

Source dependencies required to reproduce the firmware belong in the repository as pinned Git submodules when practical. Host build tools remain host-installed.

For the current baseline:

- Pico SDK: `third_party/pico-sdk` Git submodule, pinned to 2.3.0.
- CMake: host tool.
- Arm GNU Toolchain: host toolchain.
- Ninja: optional host build tool.
- Pi86 upstream source: not imported yet; licensing must be reviewed first.
- ArduinoX86: reference only, not a build dependency.

Normal builds do **not** require the `PICO_SDK_PATH` environment variable. The root CMake configuration uses the repository submodule by default. An explicit `-DPICO_SDK_PATH=...` may be supplied only when intentionally testing another SDK checkout.

## Required host tools

- Git
- CMake
- Arm GNU Toolchain supported by Pico SDK 2.3.0
- Ninja, optional but recommended

## Clone with dependencies

Preferred new clone:

```bash
git clone --recursive git@github.com:cctsao1008/pi86-rp2350.git
cd pi86-rp2350
```

For an existing clone:

```bash
git pull
git submodule update --init --recursive
```

Verify the dependency state:

```bash
git submodule status --recursive
```

The first line should show `third_party/pico-sdk` at the repository-pinned commit. Nested Pico SDK submodules should also be populated.

## Linux / WSL build

The repository includes `scripts/build.sh`.

Full build:

```bash
./scripts/build.sh
```

Clean build:

```bash
./scripts/build.sh --clean
```

Build only the Gate 0 firmware:

```bash
./scripts/build.sh --target pi86_rp2350
```

Build only the GPIO test:

```bash
./scripts/build.sh --target gpio_test
```

The script validates that `third_party/pico-sdk` is initialized before configuring CMake.

## PowerShell build

The repository includes `scripts/build.ps1`.

Full build:

```powershell
.\scripts\build.ps1
```

Clean build:

```powershell
.\scripts\build.ps1 -Clean
```

Build only the Gate 0 firmware:

```powershell
.\scripts\build.ps1 -Target pi86_rp2350
```

Build only the GPIO test:

```powershell
.\scripts\build.ps1 -Target gpio_test
```

The PowerShell helper also validates the repository submodule and does not require `PICO_SDK_PATH`.

## Manual CMake build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The root CMake configuration defaults to:

```text
PICO_SDK_PATH=<repo>/third_party/pico-sdk
PICO_BOARD=waveshare_rp2350_pizero
```

For a deliberate SDK experiment, an explicit override remains possible:

```bash
cmake -S . -B build-test \
    -DPICO_SDK_PATH=/path/to/another/pico-sdk \
    -DPICO_BOARD=waveshare_rp2350_pizero
```

Do not use an alternate SDK as the project baseline without updating the pinned submodule and documenting the change.

## Expected initial UF2 files

After a successful full build, expect files equivalent to:

```text
build/firmware/pi86_rp2350.uf2
build/tests/gpio_test/gpio_test.uf2
```

Exact auxiliary files may vary with the host toolchain.

## Gate 0 flashing

For Gate 0, the V30 HAT is not required.

1. Put the RP2350-PiZero into BOOTSEL/USB mass-storage mode.
2. Copy `pi86_rp2350.uf2` to the RP-series mass-storage device.
3. Allow the board to reboot.
4. Open the USB CDC serial device.
5. Verify the project banner and repeating `Gate 0 heartbeat` output.

The firmware prints its banner whenever a USB CDC connection is newly detected, so opening the terminal after the board has already booted should still produce visible output.

## Gate 1 safety

`gpio_test.uf2` configures GPIO0 through GPIO27 as outputs and sweeps them one at a time.

**Remove the V30 HAT before running `gpio_test`.** Use only the temporary GPIO test board for Gate 1.
