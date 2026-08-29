# Build and Toolchain

This is the canonical build procedure for `pi86-rp2350`.

## Requirements

- Git and recursive submodules
- CMake and Ninja
- Arm GNU Toolchain supported by the pinned Pico SDK
- host C toolchain, `curl`, `tar`, `make`, `pkg-config`, and libusb development files
- NASM 3.02, installed repository-locally by the bootstrap script

The target board is `waveshare_rp2350_pizero`. The repository pins Pico SDK,
picotool, FatFs, and the processor-image build path.

## Clone or update

```bash
git clone --recursive git@github.com:cctsao1008/pi86-rp2350.git
cd pi86-rp2350
```

For an existing clone:

```bash
git pull
git submodule update --init --recursive
```

## Linux or WSL build

```bash
./scripts/bootstrap_tools.sh
./scripts/build.sh --clean
```

The canonical firmware target is:

```bash
./scripts/build.sh --target rp86_rp2350
```

The UF2 is generated at:

```text
build/firmware/rp86_rp2350.uf2
```

The build also assembles the native 8086-class processor runtime and standalone
workloads with NASM. To bootstrap only NASM:

```bash
./scripts/bootstrap_nasm.sh
```

## PowerShell build

```powershell
.\scripts\build.ps1 -Clean
.\scripts\build.ps1 -Target rp86_rp2350
```

## Manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Normal builds use repository-pinned dependencies and do not require a global
`PICO_SDK_PATH`.

## Tests

```bash
python3 -m unittest discover -s tests/host_runtime -p 'test_*.py'
python3 -m unittest discover -s tests/runtime -p 'test_*.py'
python3 tools/docs/check_docs.py
```

`clock_stepped_native_runtime` remains the dedicated physical validation target
for the CLOCK_STEPPED processor-bus path. It is not the canonical Host runtime
UF2.
