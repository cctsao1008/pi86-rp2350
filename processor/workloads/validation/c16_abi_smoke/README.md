# C/16 ABI smoke validation

This workload is the physical acceptance image for Issue #58. It combines a NASM 16-bit OMF startup object with a freestanding Open Watcom C/16 object, links them at the RP86 workload base, packages the result as `C16SMOKE.P86W`, and converts the native checksum into RP86's formal `RESULT: PASS` / `RESULT: FAIL` signal.

The normal native result is `0x147A`. A BSS initialization failure returns `0xB551`.

## Reproduce the C/16 build

The canonical CI compiler input is the Open Watcom `2026-09-01-Build` release asset with a pinned SHA-256 digest. The commands below reproduce that compiler input on Linux/WSL.

```bash
set -euo pipefail

OW_TAG=2026-09-01-Build
OW_SHA256=bac354f3c75ffa49ff8d70a44e475de7e7c1823fff04b80c14787bd0792c9bdf
OW_ARCHIVE=/tmp/ow-snapshot.tar.xz

curl -fL --retry 3 --retry-delay 2 \
  -o "$OW_ARCHIVE" \
  "https://github.com/open-watcom/open-watcom-v2/releases/download/${OW_TAG}/ow-snapshot.tar.xz"

echo "${OW_SHA256}  ${OW_ARCHIVE}" | sha256sum -c -

rm -rf "$HOME/watcom"
mkdir -p "$HOME/watcom"
tar -xJf "$OW_ARCHIVE" -C "$HOME/watcom"

export WATCOM="$HOME/watcom"
export INCLUDE="$WATCOM/h"
export PATH="$WATCOM/binl:$PATH"

command -v wcc
command -v wlink
```

Install the remaining host build tools if they are not already present:

```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build nasm python3
```

Configure only the native processor workload tree and build the package:

```bash
cmake -S . -B build-c16 -G Ninja \
  -DRP86_PROCESSOR_ONLY=ON \
  -DRP86_ENABLE_PROCESSOR_C16=ON \
  -DRP86_NASM_EXECUTABLE="$(command -v nasm)" \
  -DRP86_WCC_EXECUTABLE="$(command -v wcc)" \
  -DRP86_WLINK_EXECUTABLE="$(command -v wlink)"

cmake --build build-c16 --target c16_abi_smoke_package --verbose
```

The physical workload package is:

```text
build-c16/workloads/C16SMOKE.P86W
```

The C/16 ABI itself, including the compiler switches, segmentation model, calling convention, BSS policy, and linker placement contract, is defined in [`docs/processor_c16_abi.md`](../../../../docs/processor_c16_abi.md).

## Physical Intel 8086 acceptance

Use the existing RP86 physical-regression path. From a Host environment that can access the RP2350 device:

```text
py tools\rp86.py --physical-regression "<absolute-path-to>\C16SMOKE.P86W"
```

If the Host runtime is being run directly from a Linux/WSL checkout with device access, the equivalent package argument is:

```bash
python3 tools/rp86.py --physical-regression build-c16/workloads/C16SMOKE.P86W
```

Issue #58 accepts the Intel 8086 run only when the structured workload result is `COMPLETED`, the completion reason is the native terminal-HLT path, and the firmware-owned formal PASS flag is present. Supporting evidence should show the native result `147A` and the diagnostic line `RESULT: PASS`.

A completed workload without the formal PASS flag is not acceptance. Any C return other than `0x147A` emits `RESULT: FAIL`.

## NEC V30 compatibility

After the physical Intel 8086 run passes, run the **same `C16SMOKE.P86W` package** on the NEC V30. Do not rebuild with a V30-specific compiler target. Compatibility means the common Intel-8086 ABI/image path remains valid on both physical processors.
