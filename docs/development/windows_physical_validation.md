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

Python 3.10 or newer is recommended. Live CDC capture uses `pyserial`; the
composite bridge uses `hidapi`; offline log validation uses only the Python
standard library.

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

## Send and validate AI-B1-B

Flash the `ai_bridge_live_mailbox_600khz` UF2, then run:

```powershell
py tools\ai_bridge\physical_validator.py --port COM14 --profile ai-b1-b `
  --output-dir D:\pi86-validation-logs
```

The `ai-b1-b` profile writes one canonical 64-byte binary record immediately
after opening CDC. This is application data, not a terminal command or line of
text. Firmware accepts the complete record before releasing the V30, then
withholds mailbox publication until the running V30 has physically observed
STATUS=NOT_READY. The same capture must prove the subsequent READY state,
seven data reads, V30 reply, commit, and terminal safe state.

Do not type the greeting into a terminal while this profile is running. The
profile owns the binary request and rejects output from a different ROM or
clock identity.

## Exchange over HID and explain the CDC evidence

Flash `ai_bridge_hid_mailbox_600khz.uf2`. This enumerates one composite USB
device with a generic 64-byte HID IN/OUT interface and a CDC log interface.
The development USB identity is VID `CAFE`, PID `4011`; it is not a
production-assigned identity.

Find the new CDC COM port, then confirm that the HID interface is visible:

```powershell
py tools\ai_bridge\physical_validator.py --list-ports
py tools\ai_bridge\v30bridge.py --list-devices
```

Run one physical exchange, replacing `COM14` when Windows assigns another
port:

```powershell
py tools\ai_bridge\v30bridge.py --exchange --port COM14 `
  --output-dir D:\pi86-validation-logs
```

The tool opens CDC first so firmware may finish USB initialization, sends one
exact 64-byte `HELLO NEC V30` request through HID OUT, receives the V30's exact
64-byte reply through HID IN, and captures CDC without sending application
data over it. Overall PASS requires the HID reply and CDC acceptance profile
to agree on the sequence, V30 output, 0.600 MHz engine, mailbox activity,
deadline gate, and final electrical state.

Every run preserves two artifacts:

- `.log`: the unmodified CDC byte stream and canonical physical evidence;
- `.json`: request/reply records, hashes, HID identity, every CDC check, bus
  safety, paths, and a deterministic plain-language explanation.

The console explanation is derived only from named validation checks. It tells
the physical story—reset-vector fetch, ROM execution, atomic mailbox
publication, seven V30 reads, V30 reply/commit, and terminal high-Z—but never
replaces the raw evidence. For an agent-friendly single JSON value:

```powershell
py tools\ai_bridge\v30bridge.py --exchange --port COM14 --json `
  --output-dir D:\pi86-validation-logs
```

The JSON command is the intended Codex boundary. Python remains responsible
for USB discovery and deterministic validation; Codex consumes the stable
result instead of interpreting free-form terminal text.

## Keep the 1 MHz V30 synchronized

The persistent companion runtime uses **1.000 MHz** as its default operating
point. After its bounded acceptance gate passes, enter the recommended
interactive heartbeat session:

```powershell
py tools\ai_bridge\v30bridge.py --interactive --heartbeat --port COM27 `
  --display status --interval 1.0 `
  --output-dir D:\pi86-validation-logs
```

The status display updates one terminal line instead of printing one line per
second. Normal heartbeat traffic therefore cannot hide command output. Type
`verbose` when every round should be visible, or `quiet` when only commands
and failures should be printed.

Each heartbeat contains a new 32-bit sequence and 64-bit nonce in the same
seven-word native mailbox consumed by the V30 ISR. A successful line means
all of the following happened again after the initial acceptance test:

1. Windows delivered one complete 64-byte HID record.
2. RP2350 asserted physical INTR and the V30 completed both INTA cycles.
3. The native ISR read all seven fresh words and wrote the expected XOR
   witness.
4. PIO0/DMA observed the native six-word reply commit and EOI on the bus.
5. The V30 executed IRET, invoked its foreground INT 60h service, and returned
   to the `STI`/`HLT` idle loop.
6. Only then did RP2350 return the sequence-bound HID reply.

Useful interactive commands are:

```text
ping            run one heartbeat immediately
status          show completed/lost counts and min/average/max latency
send <text>     prove consumption of up to 14 mailbox bytes by the native ISR
quiet           print commands and failures only
verbose         print every completed heartbeat
help            show commands
quit            stop Windows monitoring without resetting the V30
```

For a bounded unattended run, omit `--interactive` and specify a round count:

```powershell
py tools\ai_bridge\v30bridge.py --exchange --heartbeat --port COM27 `
  --rounds 100 --display quiet --interval 1.0 `
  --output-dir D:\pi86-validation-logs
```

The session retains a raw CDC log plus a JSON file containing every sequence,
latency, loss, and HID identity. Only one request may be outstanding; an
interactive command takes priority over the next scheduled heartbeat.
`Ctrl+C` closes the host monitor but does not assert V30 RESET.

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

Use `--profile ai-b1-b` for AI-B1-B and `--profile ai-b2-hid` for composite
CDC evidence. Offline mode does not transmit another request; it only reapplies
the named acceptance contract and prints the same deterministic explanation.

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
