#!/usr/bin/env bash
set -euo pipefail

nasm_version="3.02"
nasm_archive="nasm-${nasm_version}.tar.xz"
nasm_url="https://www.nasm.us/pub/nasm/releasebuilds/${nasm_version}/${nasm_archive}"
nasm_sha256="87336eba53b4acfe917424ab5d500d2b0054d9f5148d35c2273ccf2cfb712f0d"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
tools_root="${repo_root}/.tools"
download_dir="${tools_root}/downloads"
source_dir="${tools_root}/nasm-${nasm_version}-src"
install_dir="${tools_root}/nasm-${nasm_version}"
archive_path="${download_dir}/${nasm_archive}"
nasm_bin="${install_dir}/bin/nasm"

if [[ -x "${nasm_bin}" ]] &&
   "${nasm_bin}" -v | grep -q "NASM version ${nasm_version}"; then
    echo "Repository-local NASM ${nasm_version} is ready:"
    echo "  ${nasm_bin}"
    exit 0
fi

for tool in curl sha256sum tar make cc; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "ERROR: ${tool} is required to bootstrap NASM ${nasm_version}." >&2
        exit 1
    fi
done

mkdir -p "${download_dir}" "${tools_root}"

if [[ ! -f "${archive_path}" ]]; then
    echo "Downloading NASM ${nasm_version} from the official release archive."
    curl --fail --location --proto '=https' --tlsv1.2 \
        --output "${archive_path}.tmp" "${nasm_url}"
    mv "${archive_path}.tmp" "${archive_path}"
fi

printf '%s  %s\n' "${nasm_sha256}" "${archive_path}" | sha256sum --check --status || {
    echo "ERROR: NASM archive SHA-256 verification failed:" >&2
    echo "  ${archive_path}" >&2
    exit 1
}

rm -rf "${source_dir}" "${install_dir}"
mkdir -p "${source_dir}"
tar -xf "${archive_path}" --strip-components=1 -C "${source_dir}"

(
    cd "${source_dir}"
    ./configure --prefix="${install_dir}"
    make -j"$(getconf _NPROCESSORS_ONLN)"
    make install
)

if [[ ! -x "${nasm_bin}" ]]; then
    echo "ERROR: NASM was not installed at ${nasm_bin}." >&2
    exit 1
fi

version_output="$(${nasm_bin} -v)"
printf '%s\n' "${version_output}"
if ! grep -q "NASM version ${nasm_version}" <<<"${version_output}"; then
    echo "ERROR: expected NASM ${nasm_version}." >&2
    exit 1
fi

cat <<EOF

Repository-local NASM is ready.
  Version = ${nasm_version}
  Binary  = ${nasm_bin}
  SHA-256 = ${nasm_sha256}
EOF
