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

The public build profiles are intentionally small:

```text
firmware    RP2350 firmware
workloads   complete configured native 8086 workload set
all         firmware + workloads
```

Build generation remains separate from flashing and physical validation.

## Build trees and staged artifacts

The repository uses two canonical CMake build trees:

```text
build-firmware/     RP2350 firmware configuration
build-workloads/    processor-only workload configuration
```

`build-workloads/` is an incremental build cache, not a distributable artifact
registry. After a complete workload build, the driver clears and stages the
current package set into:

```text
artifacts/workloads/
```

Only that staged directory represents the current complete workload artifact
set. Stale `.P86W` files left in a build tree cannot enter it.

Every workload package is registered by the same CMake package helper that
creates its package target. The aggregate target is:

```text
rp86_workload_packages
```

The driver does not maintain a second list of workload names or package paths.
Each staged `.P86W` is decoded with the normal workload parser before the build
is reported PASS; the `.P86W` manifest remains the runtime authority.

## Requirements

Common requirements are Git, Python 3, CMake, and a host C toolchain. Ninja is
used automatically when available.

Profile-specific requirements are discovered and checked before CMake build
steps begin:

- `firmware`: repository-pinned Pico SDK and picotool, plus the Arm GNU
  Toolchain supported by the pinned Pico SDK. On Linux/WSL the driver can call
  `scripts/bootstrap_tools.sh` when the repository-local picotool is missing.
- `workloads`: NASM 3.02, Open Watcom C/16 `wcc` and `wlink`, and the
  repository-pinned `third_party/FreeRTOS-Kernel` revision. This single
  configuration builds the complete current ASM, C/16, and FreeRTOS workload
  package graph.

Open Watcom discovery accepts `RP86_WCC_EXECUTABLE` and
`RP86_WLINK_EXECUTABLE`, then checks `WATCOM`, `~/watcom`, the repository-local
tool area, and `PATH`.

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
./scripts/build.sh workloads
./scripts/build.sh all
```

Canonical outputs are:

```text
build-firmware/firmware/rp86_rp2350.uf2
artifacts/workloads/*.P86W
```

For the established Windows/WSL development flow, use PowerShell for Git and
Host/physical-runtime operations, and WSL for the native build toolchains.

## PowerShell

```powershell
.\scripts\build.ps1 firmware --clean
.\scripts\build.ps1 workloads
.\scripts\build.ps1 all
```

Native Windows workload builds require native Windows versions of NASM and
Open Watcom. WSL remains the canonical workload build environment when those
native tools are not installed.

## cmd.exe

```cmd
scripts\build.cmd firmware --clean
scripts\build.cmd workloads
scripts\build.cmd all
```

## Useful options

The canonical driver accepts:

```text
--clean                 remove the selected build directory before configure
--build-dir <path>      override the selected profile build directory
--target <name>         override the CMake target for expert/debug use
--jobs <n>              set the parallel build job count
--verbose               request verbose CMake build output
```

`--build-dir` and `--target` are rejected with `all` because firmware and
processor workloads are separate CMake configurations.

When `workloads --target <name>` is used, the requested target is built but the
canonical staged artifact set is intentionally left unchanged. To produce the
complete staged set, run `workloads` without a target override.

## Legacy firmware wrapper compatibility

Existing firmware invocations without an explicit profile remain accepted. For
example:

```bash
./scripts/build.sh --clean --target rp86_rp2350
```

and:

```powershell
.\scripts\build.ps1 -Clean -Target rp86_rp2350
```

continue to select the firmware profile and historical `build/` directory.
New development should use the explicit `firmware` profile.

## Manual CMake

Manual CMake remains available as the expert escape hatch. Keep processor-only
and Pico firmware builds in separate build directories.

After configuring `build-workloads/`, a single package may be rebuilt directly
without changing the canonical build interface, for example:

```bash
cmake --build build-workloads --target fast_invsqrt_package --parallel
```

A complete workload package build is:

```bash
cmake --build build-workloads --target rp86_workload_packages --parallel
```

Canonical staging is performed by `scripts/build.py workloads` using the CMake
`workloads` install component.

## Tests

```bash
python3 -m unittest discover -s tests/host_runtime -p 'test_*.py'
python3 -m unittest discover -s tests/runtime -p 'test_*.py'
python3 tools/docs/check_docs.py
```

`execution_clock_runtime` is the dedicated physical validation target for the
CLOCK_STEPPED/FREE_RUNNING controller transition. It is not the canonical Host
runtime UF2.
