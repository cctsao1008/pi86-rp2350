#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
    python_cmd="python3"
elif command -v python >/dev/null 2>&1; then
    python_cmd="python"
else
    echo "ERROR: Python 3 is required to run the RP86 build driver." >&2
    exit 1
fi

exec "${python_cmd}" "${script_dir}/build.py" "$@"
