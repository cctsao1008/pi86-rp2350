#!/usr/bin/env python3
"""Canonical cross-shell build driver for pi86-rp2350."""
from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NASM_VERSION = "3.02"
FREERTOS_COMMIT = "8be86d4a24fd4091f8f4192018423ab590f408db"
COMMANDS = ("firmware", "tick", "c16", "freertos", "all")
BUILD_DIR = {
    "firmware": "build-firmware",
    "tick": "build-tick",
    "c16": "build-c16",
    "freertos": "build-freertos",
}
TARGET = {
    "firmware": "rp86_rp2350",
    "tick": "periodic_tick_package",
    "c16": "c16_abi_smoke_package",
    "freertos": "freertos_port_validation_package",
}
ARTIFACT = {
    "firmware": "firmware/rp86_rp2350.uf2",
    "tick": "workloads/TICK.P86W",
    "c16": "workloads/C16SMOKE.P86W",
    "freertos": "workloads/FREERTOS.P86W",
}


class BuildError(RuntimeError):
    pass


def shell_join(args):
    return " ".join(shlex.quote(str(x)) for x in args)


def run(args, env, capture=False, cwd=ROOT):
    cmd = [str(x) for x in args]
    if not capture:
        print("+ " + shell_join(cmd), flush=True)
    try:
        p = subprocess.run(
            cmd,
            cwd=str(cwd),
            env=env,
            check=True,
            text=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
        )
    except FileNotFoundError as exc:
        raise BuildError(f"command not found: {cmd[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise BuildError(
            f"command failed with exit code {exc.returncode}: {shell_join(cmd)}"
        ) from exc
    return p.stdout.strip() if capture else ""


def require(name):
    path = shutil.which(name)
    if not path:
        raise BuildError(f"required host command was not found in PATH: {name}")
    return path


def as_path(value):
    path = Path(os.path.expandvars(os.path.expanduser(value)))
    return (path if path.is_absolute() else ROOT / path).resolve()


def find_tool(env, env_name, names, candidates=()):
    explicit = env.get(env_name, "").strip()
    if explicit:
        path = as_path(explicit)
        if path.is_file():
            return str(path)
        found = shutil.which(explicit, path=env.get("PATH"))
        if found:
            return found
        raise BuildError(f"{env_name} points to a missing executable: {explicit}")
    for path in candidates:
        if path.is_file():
            return str(path.resolve())
    for name in names:
        found = shutil.which(name, path=env.get("PATH"))
        if found:
            return found
    return None


def prepend(env, key, path):
    value = str(path)
    old = [x for x in env.get(key, "").split(os.pathsep) if x]
    if value not in old:
        env[key] = os.pathsep.join([value] + old)


def submodule(relative, marker, env, recursive=False):
    path = ROOT / relative
    if not (path / marker).exists():
        cmd = [require("git"), "submodule", "update", "--init"]
        if recursive:
            cmd.append("--recursive")
        cmd.append(relative)
        print(f"Initializing repository dependency: {relative}")
        run(cmd, env)
    if not (path / marker).exists():
        raise BuildError(f"submodule is missing or incomplete: {relative}")
    return path


def ensure_nasm(env):
    local = ROOT / ".tools" / f"nasm-{NASM_VERSION}" / "bin"
    candidates = [local / ("nasm.exe" if os.name == "nt" else "nasm")]
    names = ("nasm.exe", "nasm") if os.name == "nt" else ("nasm",)
    nasm = find_tool(env, "RP86_NASM_EXECUTABLE", names, candidates)
    if not nasm and os.name != "nt":
        print(f"NASM {NASM_VERSION} is missing; bootstrapping it.")
        run([require("bash"), ROOT / "scripts/bootstrap_nasm.sh"], env)
        nasm = find_tool(env, "RP86_NASM_EXECUTABLE", names, candidates)
    if not nasm:
        raise BuildError(
            f"NASM {NASM_VERSION} was not found. Install it or set "
            "RP86_NASM_EXECUTABLE."
        )
    version = run([nasm, "-v"], env, capture=True)
    if f"NASM version {NASM_VERSION}" not in version:
        raise BuildError(f"expected NASM {NASM_VERSION}, got: {version}")
    print(f"NASM         : {nasm} ({version})")
    return nasm


def watcom_candidates(env, tool):
    roots = []
    if env.get("WATCOM"):
        roots.append(as_path(env["WATCOM"]))
    roots += [Path.home() / "watcom", ROOT / ".tools/watcom"]
    dirs = ("binnt64", "binnt", "binw64", "binw") if os.name == "nt" else ("binl", "binl64")
    filename = tool + (".exe" if os.name == "nt" else "")
    return roots, [root / d / filename for root in roots for d in dirs]


def ensure_watcom(env):
    roots, wcc_candidates = watcom_candidates(env, "wcc")
    _, wlink_candidates = watcom_candidates(env, "wlink")
    names_wcc = ("wcc.exe", "wcc") if os.name == "nt" else ("wcc",)
    names_wlink = ("wlink.exe", "wlink") if os.name == "nt" else ("wlink",)
    wcc = find_tool(env, "RP86_WCC_EXECUTABLE", names_wcc, wcc_candidates)
    wlink = find_tool(env, "RP86_WLINK_EXECUTABLE", names_wlink, wlink_candidates)
    if not wcc or not wlink:
        raise BuildError(
            "Open Watcom C/16 requires both wcc and wlink. Set WATCOM, put "
            "them in PATH, or set RP86_WCC_EXECUTABLE and RP86_WLINK_EXECUTABLE.\n"
            "Checked roots: " + ", ".join(str(x) for x in roots)
        )
    known_bins = {"binl", "binl64", "binnt64", "binnt", "binw64", "binw"}
    wcc_path = Path(wcc).resolve()
    wlink_path = Path(wlink).resolve()
    wcc_root = wcc_path.parent.parent if wcc_path.parent.name.lower() in known_bins else None
    wlink_root = wlink_path.parent.parent if wlink_path.parent.name.lower() in known_bins else None
    if wcc_root and wlink_root and wcc_root != wlink_root:
        raise BuildError(f"wcc and wlink resolve to different roots: {wcc_root} vs {wlink_root}")
    watcom_root = wcc_root or wlink_root
    prepend(env, "PATH", wcc_path.parent)
    if watcom_root:
        env["WATCOM"] = str(watcom_root)
        if (watcom_root / "h").is_dir():
            prepend(env, "INCLUDE", watcom_root / "h")
    print(f"Open Watcom  : {watcom_root or '(root not inferred)'}")
    print(f"wcc          : {wcc}")
    print(f"wlink        : {wlink}")
    return wcc, wlink


def ensure_freertos(env):
    kernel = submodule(
        "third_party/FreeRTOS-Kernel", "include/FreeRTOS.h", env, recursive=False
    )
    actual = run([require("git"), "-C", kernel, "rev-parse", "HEAD"], env, capture=True)
    if actual != FREERTOS_COMMIT:
        raise BuildError(
            "FreeRTOS-Kernel is not at the repository-pinned commit.\n"
            f"expected: {FREERTOS_COMMIT}\nactual:   {actual}\n"
            "Resolve local changes, then run: git submodule update --init "
            "third_party/FreeRTOS-Kernel"
        )
    print(f"FreeRTOS     : {actual}")


def ensure_firmware(env):
    sdk = submodule("third_party/pico-sdk", "pico_sdk_init.cmake", env, recursive=True)
    submodule("third_party/picotool", "CMakeLists.txt", env, recursive=True)
    picotool_dir = as_path(env["RP86_PICOTOOL_DIR"]) if env.get("RP86_PICOTOOL_DIR") else ROOT / ".tools/picotool-install/picotool"
    picotool = picotool_dir / ("picotool.exe" if os.name == "nt" else "picotool")
    if not picotool.is_file() and os.name != "nt":
        print("picotool is missing; bootstrapping pinned host tools.")
        run([require("bash"), ROOT / "scripts/bootstrap_tools.sh"], env)
    if not picotool.is_file():
        raise BuildError(
            "validated picotool v2.3.0 was not found. On native Windows set "
            "RP86_PICOTOOL_DIR, or build from WSL/Bash."
        )
    version = run([picotool, "version"], env, capture=True)
    if "picotool v2.3.0" not in version or "compiled without USB support" in version:
        raise BuildError(f"invalid picotool build:\n{version}")
    print(f"Pico SDK     : {sdk}")
    print(f"picotool     : {picotool}")
    return picotool_dir


def cache_mode(build_dir):
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith("RP86_PROCESSOR_ONLY:") and "=" in line:
            return line.split("=", 1)[1].strip().upper()
    return None


def clean_dir(build_dir):
    resolved = build_dir.resolve()
    if resolved in (ROOT.resolve(), Path(resolved.anchor)):
        raise BuildError(f"refusing to remove unsafe build directory: {resolved}")
    if resolved.exists():
        print(f"Removing {resolved}")
        shutil.rmtree(resolved)


def configure(build_dir, defs, env):
    cmake = require("cmake")
    args = [cmake, "-S", ROOT, "-B", build_dir]
    if not (build_dir / "CMakeCache.txt").exists() and shutil.which("ninja", path=env.get("PATH")):
        args += ["-G", "Ninja"]
    args += [f"-D{k}={v}" for k, v in defs.items()]
    run(args, env)


def cmake_build(build_dir, target, env, jobs, verbose):
    args = [require("cmake"), "--build", build_dir, "--target", target, "--parallel"]
    if jobs:
        args.append(str(jobs))
    if verbose:
        args.append("--verbose")
    run(args, env)


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def report(command, build_dir, target):
    if command == "firmware" and target != TARGET["firmware"]:
        files = sorted(build_dir.rglob("*.uf2"))
        print("Generated UF2 : " + (", ".join(str(x) for x in files) or "none"))
        return
    artifact = build_dir / ARTIFACT[command]
    if not artifact.is_file():
        raise BuildError(f"expected artifact was not produced: {artifact}")
    print(f"Artifact      : {artifact}")
    print(f"Size          : {artifact.stat().st_size} bytes")
    print(f"SHA-256       : {sha256(artifact)}")
    if artifact.suffix.upper() == ".P86W":
        sys.path.insert(0, str(ROOT / "tools"))
        from rp86_runtime.workload import decode_workload_file
        manifest, image = decode_workload_file(artifact.read_bytes())
        print(f"Image         : {len(image)} bytes")
        print(f"Image CRC32   : {manifest.image_crc32:08X}")
        print(f"Load          : 0x{manifest.load_address:05X}")
        print(f"Entry         : {manifest.entry_segment:04X}:{manifest.entry_offset:04X}")
        print(f"Stack         : {manifest.stack_segment:04X}:{manifest.stack_offset:04X}")
        print(f"Flags         : 0x{manifest.flags:08X}")


def build_one(command, build_dir, target_override, args, base_env):
    env = dict(base_env)
    processor_only = command != "firmware"
    if args.clean:
        clean_dir(build_dir)
    existing = cache_mode(build_dir)
    expected = "ON" if processor_only else "OFF"
    if existing and existing != expected:
        raise BuildError(
            f"{build_dir} has RP86_PROCESSOR_ONLY={existing}, expected {expected}. "
            "Use the command-specific directory or --clean."
        )

    defs = {
        "RP86_PROCESSOR_ONLY": expected,
        "RP86_ENABLE_PROCESSOR_C16": "ON" if command == "c16" else "OFF",
        "RP86_ENABLE_FREERTOS_8086": "ON" if command == "freertos" else "OFF",
    }

    print(f"\n=== RP86 build: {command} ===")
    print(f"Repository    : {ROOT}")
    print(f"Build dir     : {build_dir}")

    if command == "firmware":
        picotool_dir = ensure_firmware(env)
        defs.update(
            PICO_BOARD="waveshare_rp2350_pizero",
            picotool_DIR=picotool_dir,
            CMAKE_BUILD_TYPE="Release",
        )
    else:
        defs["RP86_NASM_EXECUTABLE"] = ensure_nasm(env)
        if command in ("c16", "freertos"):
            wcc, wlink = ensure_watcom(env)
            defs["RP86_WCC_EXECUTABLE"] = wcc
            defs["RP86_WLINK_EXECUTABLE"] = wlink
        if command == "freertos":
            ensure_freertos(env)

    configure(build_dir, defs, env)
    target = target_override or TARGET[command]
    print(f"Target        : {target}")
    cmake_build(build_dir, target, env, args.jobs, args.verbose)
    report(command, build_dir, target)
    print(f"BUILD {command.upper()}: PASS")


def main(argv=None):
    raw = list(sys.argv[1:] if argv is None else argv)
    explicit = any(x in COMMANDS for x in raw)
    parser = argparse.ArgumentParser(
        description="Canonical pi86-rp2350 build driver used by Bash, PowerShell, and CMD."
    )
    parser.add_argument("command", nargs="?", choices=COMMANDS, default="firmware")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--build-dir")
    parser.add_argument("--target")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(raw)

    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be at least 1")
    if args.command == "all" and (args.build_dir or args.target):
        parser.error("--build-dir and --target cannot be used with 'all'")

    env = os.environ.copy()
    try:
        if args.command == "all":
            for command in ("firmware", "tick", "c16", "freertos"):
                build_one(command, ROOT / BUILD_DIR[command], None, args, env)
        else:
            if args.build_dir:
                build_dir = as_path(args.build_dir)
            elif explicit:
                build_dir = ROOT / BUILD_DIR[args.command]
            else:
                # Legacy build.sh/build.ps1 firmware invocations keep build/.
                build_dir = ROOT / "build"
            build_one(args.command, build_dir, args.target, args, env)
    except (BuildError, OSError, ImportError) as exc:
        print(f"\nBUILD ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
