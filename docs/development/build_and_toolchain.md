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
- `curl`, `tar`, `make`, and a host C compiler to bootstrap the pinned NASM
- **NASM 3.02**, installed repository-locally by the bootstrap script
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

The bootstrap downloads the official NASM 3.02 source archive, verifies
SHA-256
`87336eba53b4acfe917424ab5d500d2b0054d9f5148d35c2273ccf2cfb712f0d`,
and installs the host tool under `.tools/nasm-3.02`. It does not modify the WSL
system packages.

To prepare only NASM:

```bash
./scripts/bootstrap_nasm.sh
```

Build the initial PC1-C0C0 ROM image without changing a PC1-C0B target:

```bash
./scripts/build.sh --target pc1c0c_sram_rom_image
```

Build the physical Native BIOS `HELLO RP2350` diagnostic test:

```bash
./scripts/build.sh --target pc1c_native_bios_hello
```

The UF2 is generated at:

```text
build/tests/performance_characterization_1_extended/pc1c_native_bios_hello.uf2
```

Build the structured Native BIOS foundation image and its descriptor-fed
physical regression target:

```bash
./scripts/build.sh --target pc1c_native_bios_foundation
```

Generated files:

```text
build/tests/performance_characterization_1_extended/pc1c_native_bios_foundation.uf2
build/tests/performance_characterization_1_extended/generated/native_bios_rom/native_bios_rom.bin
```

Build the PC1-C0C1-A non-driving arbitrary-selector feasibility test:

```bash
./scripts/build.sh --target pc1c_arbitrary_rom_feasibility
```

The UF2 is generated at:

```text
build/tests/performance_characterization_1_extended/pc1c_arbitrary_rom_feasibility.uf2
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
