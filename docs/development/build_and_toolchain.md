# Build & Toolchain

This document contains the development environment, toolchain, dependency, and build instructions for `pi86-rp2350`.

## Toolchain

The project has two execution domains.

### Raspberry Pi RP2350 side

- C / C++
- Raspberry Pi Pico SDK
- Arm GNU Toolchain
- CMake
- picotool

### NEC V30 side

- 16-bit x86/V30 assembly
- **NASM** for ROM, diagnostic, monitor, and BIOS-side test images

NASM-generated flat binaries can be embedded or mapped as V30 ROM images for physical hardware execution tests.

## Build dependencies

The main Raspberry Pi RP2350 build dependencies are repository-pinned for reproducibility. Pico SDK and picotool are Git submodules; dependency initialization therefore uses `--recursive`.

### Prerequisites

- Git
- CMake
- Arm GNU Toolchain supported by the pinned Pico SDK
- `pkg-config`
- `libusb-1.0` development files
- **NASM 3.x** for V30/8086 ROM and diagnostic images
- Ninja optional but recommended

The project uses the Pico SDK board definition `waveshare_rp2350_pizero`.

## Clone

```bash
git clone --recursive git@github.com:cctsao1008/pi86-rp2350.git
cd pi86-rp2350
```

For an existing clone:

```bash
git pull
git submodule update --init --recursive
```

## Linux / WSL

```bash
./scripts/bootstrap_tools.sh
./scripts/build.sh --clean
```

Build the primary firmware:

```bash
./scripts/build.sh --target pi86_rp2350
```

## PowerShell

```powershell
.\scripts\build.ps1 -Clean
```

or:

```powershell
.\scripts\build.ps1 -Target pi86_rp2350
```

## Manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Normal builds use the repository-pinned Pico SDK and do not require a global `PICO_SDK_PATH`.
