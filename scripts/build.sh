#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
build_dir="${repo_root}/build"
target=""
clean=0

usage() {
    cat <<'EOF'
Usage: ./scripts/build.sh [options]

Options:
  --clean              Remove the build directory before configuring.
  --target <name>      Build only the named CMake target.
  --build-dir <path>   Use a different build directory.
  -h, --help           Show this help text.
EOF
}

while (($# > 0)); do
    case "$1" in
        --clean)
            clean=1
            shift
            ;;
        --target)
            if (($# < 2)); then
                echo "ERROR: --target requires a value" >&2
                exit 2
            fi
            target="$2"
            shift 2
            ;;
        --build-dir)
            if (($# < 2)); then
                echo "ERROR: --build-dir requires a value" >&2
                exit 2
            fi
            if [[ "$2" = /* ]]; then
                build_dir="$2"
            else
                build_dir="${repo_root}/$2"
            fi
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

sdk_path="${repo_root}/third_party/pico-sdk"
sdk_init="${sdk_path}/pico_sdk_init.cmake"
picotool_src="${repo_root}/third_party/picotool"
picotool_install="${repo_root}/.tools/picotool-install"
picotool_cmake_dir="${picotool_install}/picotool"
picotool_bin="${picotool_cmake_dir}/picotool"

if [[ ! -f "$sdk_init" || ! -f "${picotool_src}/CMakeLists.txt" ]]; then
    cat >&2 <<EOF
ERROR: repository submodules are missing or incomplete.

Run from the repository root:
  git submodule update --init --recursive
EOF
    exit 1
fi

if [[ ! -x "${picotool_bin}" ]]; then
    echo "Repository-local picotool is not bootstrapped; building it now."
    "${repo_root}/scripts/bootstrap_tools.sh"
fi

picotool_version="$(${picotool_bin} version 2>&1)"
if ! grep -q 'picotool v2\.3\.0' <<<"${picotool_version}"; then
    echo "ERROR: repository-local picotool is not v2.3.0:" >&2
    printf '%s\n' "${picotool_version}" >&2
    echo "Run ./scripts/bootstrap_tools.sh to rebuild the pinned host tool." >&2
    exit 1
fi

if grep -q 'compiled without USB support' <<<"${picotool_version}"; then
    echo "ERROR: repository-local picotool lacks USB support." >&2
    echo "Run ./scripts/bootstrap_tools.sh after installing libusb development files." >&2
    exit 1
fi

if ((clean)) && [[ -d "$build_dir" ]]; then
    echo "Removing ${build_dir}"
    rm -rf "$build_dir"
fi

echo "Repository = ${repo_root}"
echo "Pico SDK   = ${sdk_path}"
echo "picotool   = ${picotool_bin}"
echo "PICO_BOARD = waveshare_rp2350_pizero"
echo "BuildDir   = ${build_dir}"

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -DPICO_BOARD=waveshare_rp2350_pizero
    -Dpicotool_DIR="${picotool_cmake_dir}"
    -DCMAKE_BUILD_TYPE=Release
)

if command -v ninja >/dev/null 2>&1; then
    cmake_args+=( -G Ninja )
fi

cmake "${cmake_args[@]}"

build_args=(--build "$build_dir" --parallel)
if [[ -n "$target" ]]; then
    build_args+=(--target "$target")
fi

cmake "${build_args[@]}"

echo
echo "Generated UF2 files:"
find "$build_dir" -type f -name '*.uf2' -print 2>/dev/null | sort || true
