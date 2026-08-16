#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
sdk_path="${repo_root}/third_party/pico-sdk"
picotool_src="${repo_root}/third_party/picotool"
tools_root="${repo_root}/.tools"
picotool_build="${tools_root}/picotool-build"
picotool_install="${tools_root}/picotool-install"
picotool_cmake_dir="${picotool_install}/picotool"
picotool_bin="${picotool_cmake_dir}/picotool"

if [[ ! -f "${sdk_path}/pico_sdk_init.cmake" ]]; then
    cat >&2 <<EOF
ERROR: Pico SDK submodule is missing or incomplete:
  ${sdk_path}

Run from the repository root:
  git submodule update --init --recursive
EOF
    exit 1
fi

if [[ ! -f "${picotool_src}/CMakeLists.txt" ]]; then
    cat >&2 <<EOF
ERROR: picotool submodule is missing or incomplete:
  ${picotool_src}

Run from the repository root:
  git submodule update --init --recursive
EOF
    exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "ERROR: pkg-config is required." >&2
    exit 1
fi

if ! pkg-config --exists libusb-1.0; then
    cat >&2 <<'EOF'
ERROR: libusb-1.0 development files were not found.
On Ubuntu/WSL install them with:
  sudo apt install libusb-1.0-0-dev pkg-config
EOF
    exit 1
fi

mkdir -p "${tools_root}"
rm -rf "${picotool_build}" "${picotool_install}"

cmake -S "${picotool_src}" -B "${picotool_build}" \
    -DPICO_SDK_PATH="${sdk_path}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${picotool_install}" \
    -DPICOTOOL_FLAT_INSTALL=1 \
    -DPICOTOOL_NO_LIBUSB=0

cmake --build "${picotool_build}" --parallel
cmake --install "${picotool_build}"

if [[ ! -x "${picotool_bin}" ]]; then
    echo "ERROR: picotool was not installed at ${picotool_bin}" >&2
    exit 1
fi

version_output="$(${picotool_bin} version 2>&1)"
printf '%s\n' "${version_output}"

if ! grep -q 'picotool v2\.3\.0' <<<"${version_output}"; then
    echo "ERROR: expected picotool v2.3.0." >&2
    exit 1
fi

if grep -q 'compiled without USB support' <<<"${version_output}"; then
    echo "ERROR: repository-local picotool was built without USB support." >&2
    exit 1
fi

cat <<EOF

Repository-local picotool is ready.
  Source  = ${picotool_src}
  Binary  = ${picotool_bin}
  CMake   = ${picotool_cmake_dir}
EOF
