# Windows Physical Validation Host

Windows is the canonical USB/CDC validation host for `pi86-rp2350`. The WSL
clone remains the Pico SDK build environment; a synchronized Windows clone
owns COM-port discovery, raw evidence capture, and host-side acceptance.

```text
WSL clone     source, firmware build, UF2, unit tests
Windows clone COM port, raw CDC evidence, physical profile validation
```

The validator is deliberately independent of Codex and does not interpret a
firmware build as physical success. It accepts only a complete CDC transcript
matching a named physical-validation profile.

## One-time setup

From PowerShell in the Windows clone:

```powershell
cd D:\my-github\pi86-rp2350
git pull origin main
py -m pip install -r tools\ai_bridge\requirements.txt
```

Python 3.10 or newer is recommended. Live capture uses `pyserial`; offline log
validation uses only the Python standard library.

## Find the RP2350 COM port

```powershell
py tools\ai_bridge\physical_validator.py --list-ports
```

The command prints the Windows device name, description, and hardware ID. Pass
the selected name explicitly so another USB serial device cannot silently
become the evidence source.

## Capture and validate AI-B1-A

Flash `ai_bridge_runtime_mailbox_600khz-6e82ae1.uf2`, then run:

```powershell
py tools\ai_bridge\physical_validator.py --port COM7 --profile ai-b1-a `
  --output-dir D:\pi86-validation-logs
```

Replace `COM7` with the enumerated device. The validator opens the CDC port,
prints incoming output unchanged, and tolerates a temporary disconnect on the
same port. If no output appears, reset or reconnect the RP2350 while the tool
is armed.

Capture ends only after the terminal electrical-state marker is received or
the timeout expires. The raw byte stream is saved before acceptance is
evaluated. The default timeout is 60 seconds and can be changed explicitly:

```powershell
py tools\ai_bridge\physical_validator.py --port COM7 --timeout 120
```

When `--output-dir` is omitted, logs are written under
`%USERPROFILE%\Documents\pi86-validation-logs`. The
`PI86_VALIDATION_LOG_DIR` environment variable can change this default.

## Revalidate saved evidence

An existing capture can be checked without hardware or `pyserial`:

```powershell
py tools\ai_bridge\physical_validator.py `
  --input D:\pi86-validation-logs\ai_b1a_YYYYMMDD_HHMMSS+0800.log
```

The AI-B1-A profile checks the exact 0.600 MHz engine identity, canonical V30
reply, reset/ROM path, Core1-to-Core0 ownership transfer, all seven mailbox
reads, XOR witness, commit, qualified-pair counts, drained DMA streams, zero
deadline misses, ROM identity, and terminal electrical state. Any `FAIL` or
`INVALID` token rejects the transcript.

## Exit status

| Code | Meaning |
|---:|---|
| `0` | Capture/profile validation passed |
| `2` | Command-line usage error |
| `3` | Serial dependency or COM-port discovery error |
| `4` | Input/capture error or terminal-marker timeout |
| `5` | Complete transcript failed its acceptance profile |

Automation must use the process exit status, not search only for a greeting.
A visible `HELLO OPENAI CODEX` is necessary but is not sufficient physical
evidence.

## Regression tests

Run the protocol and physical-profile tests from either clone:

```powershell
py -m unittest discover -s tests\ai_bridge -p "test_*.py"
```

The physical-profile tests revalidate the committed AI-B1-A evidence and inject
bad timing, failure tokens, and incomplete terminal state. New AI-B1-B/C
profiles must add their own committed evidence and negative tests without
weakening AI-B1-A.
