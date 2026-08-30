# Windows Physical Validation

Windows owns live USB access and retained physical evidence. WSL owns the
reproducible firmware build.

```text
WSL clone      source, NASM, Pico SDK build, UF2
Windows clone  CDC/HID discovery, RP86 shell, evidence capture
```

## Setup

```powershell
cd D:\my-github\pi86-rp2350
git pull origin main
py -m pip install -r tools\rp86_runtime\requirements.txt
```

Python 3.10 or newer is recommended.

## Discover devices

The Host automatically selects a single matching CDC interface. Multiple
candidates require an explicit `--port` so multiple RP86 systems can operate
independently.

```powershell
py tools\rp86.py --list-devices
```

## Runtime status

```powershell
py tools\rp86.py --status --timeout 5
```

Use `--port COM27` only when automatic selection is ambiguous.

## Interactive physical runtime

```powershell
py tools\rp86.py --interactive --heartbeat --attach `
  --display live --interval 1.0 `
  --output-dir D:\pi86-validation-logs
```

`live` is the default interactive renderer. It uses Rich panels and color on
PowerShell, Windows Terminal, Bash, WSL, and SSH terminals. `plain` keeps the
same runtime events as redirect-safe lines; non-TTY output automatically avoids
the live panel. Existing `status` remains an alias for the live renderer.

The native `AAD 16` witness identifies Intel 8086 or NEC V30 behavior. The
optional `--processor intel-8086` or `--processor nec-v30` argument turns that
observation into a strict Host assertion; it is not required for normal use.

Useful shell commands include:

```text
status, top, info
load, run, stop, restart
ping, quiet, verbose
mem, ls, cat, put, get, df
timeout, trace, bootloader
```

## UF2 bootloader

```powershell
py tools\rp86.py --bootloader --timeout 5
```

A CDC disconnect immediately after the acknowledged request is expected while
the RP2350 enters ROM USB boot mode.

## Evidence

An interactive or bounded heartbeat session retains:

- a raw CDC `.log` containing physical observations;
- a `.json` session record containing identity, sequence, latency, loss, and
  paths to retained evidence.

HID carries command/result records. CDC carries the longer physical evidence.
A successful Host result alone is not a physical PASS; the native execution
and passive bus evidence must agree.

## Disconnect behavior

Only one process owns the physical CDC interface. The first RP86 process also
acts as the local broker; later processes connect to it by runtime ID. Loss of
CDC or HID closes the session and preserves partial evidence instead of
emitting an uncontrolled traceback.
