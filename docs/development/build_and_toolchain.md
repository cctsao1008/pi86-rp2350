# Build and Toolchain

This is the canonical build procedure for `pi86-rp2350`.

## Build entry points

All supported shells delegate to one build implementation:

```text
scripts/build.py      canonical build logic
scripts/build.sh      Bash / WSL wrapper
scripts/build.ps1     PowerShell wrapper
scripts/build.cmd     cmd.exe wrapper
```

The wrappers expose the same build profiles:

```text
firmware   RP2350 firmware (`rp86_rp2350`)
tick       periodic Intel 8086 tick validation workload
c16        Open Watcom C/16 ABI smoke workload
freertos   FreeRTOS Intel 8086 validation workload
all        run all four profiles sequentially
```

Explicit build profiles use isolated CMake build directories so firmware and
processor-only configuration caches cannot contaminate each other:

```text
build-firmware/
build-tick/
build-c16/
build-freertos/
```

Build generation is intentionally separate from flashing and physical
validation.

## Requirements

Common requirements are Git, Python 3, CMake, and a host C toolchain. Ninja is
used automatically when available.

Profile-specific requirements are discovered and checked before CMake build
steps begin:

- `firmware`: repository-pinned Pico SDK and picotool, plus the Arm GNU
  Toolchain supported by the pinned Pico SDK. On Linux/WSL the driver can call
  `scripts/bootstrap_tools.sh` when the repository-local picotool is missing.
- `tick`: NASM 3.02. On Linux/WSL the driver can call
  `scripts/bootstrap_nasm.sh` when the pinned repository-local NASM is missing.
- `c16`: NASM 3.02 plus Open Watcom C/16 `wcc` and `wlink`.
- `freertos`: the same C/16 tools plus the repository-pinned
  `third_party/FreeRTOS-Kernel` revision.

Open Watcom discovery accepts `RP86_WCC_EXECUTABLE` and
`RP86_WLINK_EXECUTABLE`, then checks `WATCOM`, `~/watcom`, the repository-local
tool area, and `PATH`. This avoids passing empty tool paths into CMake.

## Clone or update

```bash
git clone --recursive git@github.com:cctsao1008/pi86-rp2350.git
cd pi86-rp2350
```

For an existing clone:

```bash
git pull --ff-only
```

The build driver initializes the submodule needed by the selected profile when
it is missing. A full dependency initialization remains available with:

```bash
git submodule update --init --recursive
```

## Bash / WSL

```bash
./scripts/build.sh firmware --clean
./scripts/build.sh tick
./scripts/build.sh c16
./scripts/build.sh freertos
./scripts/build.sh all
```

The primary outputs are:

```text
build-firmware/firmware/rp86_rp2350.uf2
build-tick/workloads/TICK.P86W
build-c16/workloads/C16SMOKE.P86W
build-freertos/workloads/FREERTOS.P86W
```

## PowerShell

```powershell
.\scripts\build.ps1 firmware --clean
.\scripts\build.ps1 tick
.\scripts\build.ps1 c16
.\scripts\build.ps1 freertos
.\scripts\build.ps1 all
```

Native Windows builds still require native Windows versions of the selected
profile's toolchain. For the existing WSL development environment, Bash/WSL is
the canonical place to generate firmware and 8086 workload artifacts;
PowerShell remains suitable for Host runtime and physical-validation commands.

## cmd.exe

```cmd
scripts\build.cmd firmware --clean
scripts\build.cmd tick
scripts\build.cmd c16
scripts\build.cmd freertos
scripts\build.cmd all
```

## Useful options

The canonical driver accepts:

```text
--clean                 remove the selected build directory before configure
--build-dir <path>      override the profile build directory
--target <name>         override the CMake target
--jobs <n>              set the parallel build job count
--verbose               request verbose CMake build output
```

`--build-dir` and `--target` are intentionally rejected with `all` because the
profiles use incompatible CMake configuration modes.

## Legacy wrapper compatibility

Existing firmware invocations remain accepted. For example:

```bash
./scripts/build.sh --clean --target rp86_rp2350
```

and:

```powershell
.\scripts\build.ps1 -Clean -Target rp86_rp2350
```

continue to select the firmware profile and the historical `build/` directory.
New development should use an explicit profile so the isolated build
directories are used.

## Manual CMake

Manual CMake remains available for debugging, but is not the normal operator
interface. Keep processor-only and Pico firmware builds in separate build
directories and pass explicit tool paths for C/16 work.

## Tests

```bash
python3 -m unittest discover -s tests/host_runtime -p 'test_*.py'
python3 -m unittest discover -s tests/runtime -p 'test_*.py'
python3 tools/docs/check_docs.py
```

`execution_clock_runtime` is the dedicated physical validation target for the
CLOCK_STEPPED/FREE_RUNNING controller transition. It is not the canonical Host
runtime UF2.
