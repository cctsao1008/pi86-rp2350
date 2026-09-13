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

The public build profiles are intentionally configuration-level, not
per-workload:

```text
firmware    RP2350 firmware (`rp86_rp2350`)
workloads   complete configured physical-processor workload package set
all         firmware followed by workloads
```

This keeps workload inventory in the processor CMake graph. Adding a workload
that uses an existing packaging backend does not require a new public build
profile or another entry in `scripts/build.py`.

The canonical build trees are:

```text
build-firmware/
build-workloads/
```

`build-workloads/` is build cache/intermediate output. It is not the artifact
interface consumed by operators or runtime tooling.

## Canonical workload artifacts

A canonical workload build collects every configured package target through
`rp86_workload_packages`, then stages the resulting packages with CMake install
semantics into:

```text
artifacts/workloads/
```

The staging directory is removed before each canonical staging pass, so stale
`.P86W` files from an older build cannot masquerade as current output. Every
staged package is decoded with the runtime workload parser before the build is
reported successful.

The `.P86W` file remains the authoritative runtime artifact. The build system
does not maintain a second hand-authored workload catalog or duplicate package
metadata in Python.

Build completeness, runtime/UI selection, and release publication are separate
concerns. A package being present in `artifacts/workloads/` means it belongs to
the configured engineering build, not that it must be exposed in an end-user
menu.

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
- `workloads`: NASM 3.02, Open Watcom C/16 `wcc` and `wlink`, and the
  repository-pinned `third_party/FreeRTOS-Kernel` revision. These tools cover
  the complete currently configured ASM and C/16 workload set in one CMake
  configuration.

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
./scripts/build.sh workloads
./scripts/build.sh all
```

The primary outputs are:

```text
build-firmware/firmware/rp86_rp2350.uf2
artifacts/workloads/*.P86W
```

For the existing WSL development environment, Bash/WSL is the canonical place
to generate firmware and 8086 workload artifacts.

## PowerShell

```powershell
.\scripts\build.ps1 firmware --clean
.\scripts\build.ps1 workloads
.\scripts\build.ps1 all
```

Native Windows builds still require native Windows versions of the selected
profile's toolchain. PowerShell remains suitable for Host runtime and physical
validation commands when the workload toolchain itself is supplied through
WSL.

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
--build-dir <path>      override one profile's CMake build directory
--target <name>         build a specific CMake target for developer/debug work
--jobs <n>              set the parallel build job count
--verbose               request verbose CMake build output
```

`--build-dir` and `--target` are intentionally rejected with `all` because
`firmware` and `workloads` use different CMake configuration modes.

A custom workload `--target` is an escape hatch. It builds the requested CMake
target but deliberately does not replace the canonical staged
`artifacts/workloads/` set.

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
New development should use an explicit public profile.

Historical per-workload public profiles are not part of the canonical
interface. Use `workloads` for a complete artifact set, or an explicit CMake
`--target` only for focused developer/debug work.

## Manual CMake

Manual CMake remains available for debugging, but is not the normal operator
interface. Keep processor-only and Pico firmware builds in separate build
directories and pass explicit tool paths for C/16 work.

The aggregate workload package target is:

```text
rp86_workload_packages
```

## Tests

```bash
python3 -m unittest discover -s tests/host_runtime -p 'test_*.py'
python3 -m unittest discover -s tests/runtime -p 'test_*.py'
python3 tools/docs/check_docs.py
```

Build-pipeline policy tests enforce the public profiles, canonical staging
path, aggregate package registration, absence of a per-workload Python
inventory, clean install/verification staging, and
`all = firmware + complete workload set` dispatch semantics.

`execution_clock_runtime` is the dedicated physical validation target for the
CLOCK_STEPPED/FREE_RUNNING controller transition. It is not the canonical Host
runtime UF2.
