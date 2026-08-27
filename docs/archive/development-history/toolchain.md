# Toolchain and Build Setup

## Baseline

This project uses the official Pico SDK board definition:

```text
waveshare_rp2350_pizero
```

The repository pins both target SDK source and the matching host-side picotool source:

```text
third_party/pico-sdk   -> Pico SDK 2.3.0
third_party/picotool   -> picotool 2.3.0
```

Pinned commits:

```text
Pico SDK 2.3.0:
98a542c1a62fb549ffb5d66a3e5892b06276b670

picotool 2.3.0:
6f6458d792b93685a11423b244a585eaa99eafcf
```

The Pico SDK itself contains nested submodules such as TinyUSB and mbedTLS, so always initialize repository dependencies recursively.

## Dependency policy

The two submodules have different roles:

- `third_party/pico-sdk` is a target firmware source dependency.
- `third_party/picotool` is pinned host-tool source provenance.

The picotool source tree is not linked into the target firmware. `scripts/bootstrap_tools.sh` builds and installs the pinned source into the gitignored repository-local tool area:

```text
.tools/picotool-install
```

This keeps source provenance reproducible while keeping generated host binaries out of Git.

Other host requirements remain host-installed:

- CMake
- Arm GNU Toolchain
- pkg-config
- libusb-1.0 development files
- Ninja, optional but recommended

Pi86 upstream source is not imported yet; licensing must be reviewed first. ArduinoX86 remains a reference only, not a build dependency.

Normal firmware builds do **not** require `PICO_SDK_PATH` or a machine-global picotool installation. The build helper selects the repository-pinned SDK and repository-local picotool automatically.

## Required host packages on Ubuntu / WSL

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    pkg-config \
    libusb-1.0-0-dev \
    cmake
```

Ninja is optional:

```bash
sudo apt install -y ninja-build
```

The pinned picotool build requires libusb support. A picotool binary that reports `compiled without USB support` is not accepted as the project baseline.

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

Verify dependency state:

```bash
git submodule status --recursive
```

The top-level output should include both `third_party/pico-sdk` and `third_party/picotool` at the repository-pinned commits.

## Repository-local picotool bootstrap

Bootstrap explicitly with:

```bash
./scripts/bootstrap_tools.sh
```

The script:

1. validates the Pico SDK and picotool submodules;
2. requires host `pkg-config` and libusb development files;
3. builds the pinned picotool source against the pinned Pico SDK;
4. installs it under `.tools/picotool-install` using a flat install;
5. verifies picotool version 2.3.0;
6. rejects a build that lacks USB support.

Expected binary:

```text
.tools/picotool-install/bin/picotool
```

Expected CMake package directory:

```text
.tools/picotool-install/picotool
```

Verify manually:

```bash
.tools/picotool-install/bin/picotool version
```

Expected version:

```text
picotool v2.3.0
```

The output must not contain:

```text
This version of picotool was compiled without USB support.
```

## Linux / WSL build

The repository includes `scripts/build.sh`.

On the first build, if the repository-local picotool has not yet been bootstrapped, `build.sh` invokes `bootstrap_tools.sh` automatically.

Full build:

```bash
./scripts/build.sh
```

Clean build:

```bash
./scripts/build.sh --clean
```

Build one target:

```bash
./scripts/build.sh --target gate9r_pic
```

The build helper passes the repository-local package location to CMake:

```text
picotool_DIR=<repo>/.tools/picotool-install/picotool
```

Therefore a clean firmware build does not download and rebuild another picotool copy inside `build/_deps`.

## PowerShell build

The repository also includes `scripts/build.ps1` for firmware builds. The repository-local host-tool bootstrap described above is currently the canonical Linux/WSL path. Keep PowerShell usage separate until an equivalent native Windows bootstrap path is validated.

## Manual CMake build

For a manual build after `bootstrap_tools.sh`:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -Dpicotool_DIR="$PWD/.tools/picotool-install/picotool"
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
    -DPICO_BOARD=waveshare_rp2350_pizero \
    -Dpicotool_DIR="$PWD/.tools/picotool-install/picotool"
```

Do not use an alternate SDK or picotool as the project baseline without updating the pinned submodule and documenting the change.

## WSL USB note

Building picotool with libusb enables its USB-capable commands, but WSL must also be given access to the physical RP2350 USB device before commands such as `picotool info`, `load`, or `reboot` can reach the board. Windows-to-WSL USB attachment is a separate host configuration concern from picotool compilation.

## Generated UF2 files

After a successful build, target-specific UF2 files are produced below `build/`. For example:

```text
build/tests/gate9r_pic/gate9r_pic.uf2
```

## Gate 0 flashing

For Gate 0, the V30 HAT is not required.

1. Put the RP2350-PiZero into BOOTSEL/USB mass-storage mode.
2. Copy `pi86_rp2350.uf2` to the RP-series mass-storage device.
3. Allow the board to reboot.
4. Open the USB CDC serial device.
5. Verify the project banner and repeating `Gate 0 heartbeat` output.

## Gate 1 safety

`gpio_test.uf2` configures GPIO0 through GPIO27 as outputs and sweeps them one at a time.

**Remove the V30 HAT before running `gpio_test`.** Use only the temporary GPIO test board for Gate 1.
