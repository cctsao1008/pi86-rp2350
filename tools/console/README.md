# pi86 Console

`pi86_console.py` is the host-side stdin/stdout bridge for the RP2350 USB CDC interface used by `pi86-rp2350`.

The intended console path is:

```text
NEC V30
  -> pi86 virtual BIOS
  -> RP2350
  -> USB CDC
  -> pi86_console.py
  -> host terminal
```

For ELKS bring-up, BIOS teletype output (`INT 10h`, `AH=0Eh`) can be forwarded through the existing pi86 debug/console path to RP2350 USB CDC. Host keyboard input can later be returned through the same transport for BIOS keyboard services such as `INT 16h`.

## Requirements

- Python 3.10 or newer
- `pyserial`

Install dependencies:

```bash
python3 -m pip install -r tools/console/requirements.txt
```

## Usage

Auto-detect the serial port when there is one unambiguous RP2350/CDC candidate:

```bash
python3 tools/console/pi86_console.py
```

Specify a device explicitly:

```bash
python3 tools/console/pi86_console.py /dev/ttyACM0
```

On Windows:

```powershell
python tools/console/pi86_console.py COM5
```

List detected serial ports:

```bash
python3 tools/console/pi86_console.py --list
```

Exit an interactive session with `Ctrl-]`.

## Notes

The baud-rate argument is retained for serial API compatibility. For USB CDC ACM, the configured baud rate normally does not determine the physical USB transfer rate.

The tool intentionally remains a byte-transparent bridge. BIOS, ELKS, and future diagnostic protocols should define their own framing or terminal semantics rather than embedding them in this host transport layer.
