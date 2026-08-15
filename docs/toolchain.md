# Toolchain and Build Setup

## Baseline

This project targets the official Pico SDK board definition:

```text
waveshare_rp2350_pizero
```

Use Raspberry Pi Pico SDK **2.3.0 or newer**. SDK 2.3.0 added the board definition for the Waveshare RP2350-PiZero, including the RP2350B package selection and 16 MB onboard flash configuration.

## Required tools

- Git
- CMake
- Arm GNU Toolchain supported by the installed Pico SDK
- Raspberry Pi Pico SDK 2.3.0+

Ninja is optional but recommended when available.

## Obtain Pico SDK

Example:

```powershell
git clone --branch 2.3.0 --recursive https://github.com/raspberrypi/pico-sdk.git C:\pico\pico-sdk
```

If the SDK is already cloned:

```powershell
cd C:\pico\pico-sdk
git fetch --tags
git checkout 2.3.0
git submodule update --init --recursive
```

A newer compatible SDK release may also be used.

## Set `PICO_SDK_PATH`

For the current PowerShell session:

```powershell
$env:PICO_SDK_PATH = "C:\pico\pico-sdk"
```

Verify:

```powershell
Test-Path "$env:PICO_SDK_PATH\external\pico_sdk_import.cmake"
```

Expected result:

```text
True
```

## Clone and build this project

```powershell
git clone https://github.com/cctsao1008/pi86-rp2350.git
cd pi86-rp2350

cmake -S . -B build -DPICO_BOARD=waveshare_rp2350_pizero
cmake --build build --parallel
```

The root `CMakeLists.txt` defaults to `waveshare_rp2350_pizero`, so this is also valid:

```powershell
cmake -S . -B build
cmake --build build --parallel
```

## PowerShell helper

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

## Expected initial UF2 files

After a successful full build, expect files equivalent to:

```text
build\firmware\pi86_rp2350.uf2
build\tests\gpio_test\gpio_test.uf2
```

Exact auxiliary files may vary with the SDK/toolchain.

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
