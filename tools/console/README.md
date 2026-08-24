# Legacy CDC Byte Console

`pi86_console.py` is a byte-transparent USB CDC terminal retained for historical
BIOS/ELKS experiments and simple diagnostics.

It is **not** the primary Host runtime interface. New work should use the Host
Runtime Shell described in
[`docs/host_runtime_shell.md`](../../docs/host_runtime_shell.md) and the
language-independent [Host Protocol](../../docs/host_protocol.md).

The legacy path is:

```text
V30 diagnostic or compatibility workload
  -> RP2350 CDC byte stream
  -> pi86_console.py
  -> Host terminal
```

## Requirements

- Python 3.10 or newer
- `pyserial`

Install dependencies:

```bash
python3 -m pip install -r tools/console/requirements.txt
```

## Usage

Auto-detect an unambiguous RP2350 CDC port:

```bash
python3 tools/console/pi86_console.py
```

Specify a device:

```bash
python3 tools/console/pi86_console.py /dev/ttyACM0
```

On Windows:

```powershell
python tools/console/pi86_console.py COM5
```

List detected ports:

```bash
python3 tools/console/pi86_console.py --list
```

Exit with `Ctrl-]`.

## Boundary

This tool does not implement workload loading, shared files, PSRAM access,
status, timeout, or restart. It remains useful as a raw compatibility utility,
but it must not be presented as the Host-Managed Bare-Metal Physical Processor
Runtime shell.
