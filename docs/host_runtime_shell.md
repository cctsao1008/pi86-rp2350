# RP86 Host Runtime Shell

The 8086-class interface has no CPUID instruction. Instead, the canonical
native runtime executes a branch-free `AAD 16` discriminator and returns the
result in every committed 64-byte witness. Firmware runs this diagnostic at
boot without waiting for CDC or a Host HID request, then retains the identity
in the canonical 64-byte structured status/result record. RP86 uses that physical evidence to
identify the processor automatically. An optional `--processor intel-8086` or
`--processor nec-v30` argument is a strict assertion and is accepted only when
it matches the native result.

**RP86** is the Host runtime and small remote shell for a real Intel 8086 or NEC V30. It is
not an operating system running on the processor. The Host provides control and
runtime services; the RP2350 owns resources and the physical bus; the processor executes
bare-metal native workloads.

The shell is the reference interface to the
**Host-Managed Bare-Metal Physical Processor Runtime**. It is not the wire
protocol and it is not required to be implemented in Python forever.

`RPBridge` is the narrower transport layer beneath RP86: composite CDC/HID,
the 64-byte ABI, and the local multi-client broker. `tools/rp86.py` is the only
command-line entry point.

The shell shape is defined before every backend is complete so later PSRAM,
NOR Flash, SD Card, trace, and debugger work can attach to a stable interface.
An unavailable operation must return `NOT AVAILABLE`; it must never report a
successful hardware operation that did not occur.

## Resource model

```text
Host shell
   |
   +-- workload control ------ load / run / stop / restart
   +-- console --------------- send / stdin / stdout
   +-- live observation ------ status / top / trace / timeout
   +-- processor-visible memory -- mem read / write / load / save
   `-- RP2350-owned storage
          +-- flash: --------- built-in shared persistent FAT volume
          `-- sd: ------------ removable shared FAT volume
```

Both the Host and physical processor are clients of RP2350-owned memory and storage services.
Neither client directly owns a Flash controller, SD controller, filesystem
metadata, PSRAM allocator, or bus-controller state.

## Storage roles

- `flash:` is the built-in persistent FAT filesystem in External NOR Flash. It is
  intended for workloads, configuration, input, output, and persistent data.
- `sd:` is removable, larger-capacity FAT storage for workload libraries, datasets,
  trace export, snapshots, import, and backup.
- Removing or omitting SD must not prevent `flash:`, PSRAM execution, console,
  monitoring, or restart from operating.
- Filesystem requests are serialized by the RP2350. "Shared" means shared file
  contents and namespace, not concurrent raw-media ownership.

The public names are semantic and stable:

```text
flash:/hello.bin
sd:/datasets/input.dat
```

They are not DOS drive letters and do not expose Linux block-device names.
FatFs may use numeric logical drives internally, but values such as `0:` and
`1:` are private implementation details. The External NOR Flash volume is the
physically validated `RP-FLASH` FAT16 filesystem in the final 12 MiB of the
onboard W25Q128JV. Its Flash-aware block layer uses 512-byte logical sectors,
2 KiB allocation units, and 4 KiB erase alignment. Host shell file commands are
still a separate integration step; an SD card will use its normal sector
interface.

## Shell framework

| Area | Commands |
|---|---|
| Workload | `load`, `run`, `stop`, `restart` |
| Native service | `calc` |
| Console | `send`, `console`, `stdin`, `stdout` |
| Files | `ls`, `cat`, `put`, `get`, `rm`, `mv` |
| Storage | `df`, `mount`, `unmount`, `sync` |
| Memory | `mem read`, `mem write`, `mem load`, `mem save` |
| Observation | `status`, `top`, `info`, `trace`, `regs` |
| Diagnostics and supervision | `probe`, `timeout`, restart, `bootloader` |
| Shell | `help`, `pwd`, `cd`, `quiet`, `verbose`, `quit` |

The canonical RP-FLASH Host service currently implements four device-backed
commands. `ls` also lists directories on the Host running the Python shell:

```text
ls [flash:/path|<Host path>]
df [flash:]
cat <flash:/file>
put <host-file> <flash:/path>
```

`<host-file>` means a file that already exists on the Windows Host. From the
repository root, for example:

```text
put README.md flash:/README.TXT
ls C:\Users
ls D:/my-github
ls /home/build/github
```

The first two `ls` examples are Windows paths; the third is a native Linux/WSL
path. A bare Windows drive such as `ls C:` means its root. With no argument,
`ls` continues to mean `ls flash:/`. Device paths are transported over HID;
Host paths are read locally and never sent to the RP2350.

Interactive Tab completion covers command names, Host paths, and `flash:/`
entries. One match is inserted immediately; multiple matches are printed above
the persistent runtime prompt. This line editor is implemented on both
Windows and POSIX Hosts, so completion does not depend on a particular shell.
The Up and Down arrow keys traverse in-memory command history; moving past the
newest entry restores the unfinished draft that was present before navigation.
Adjacent duplicate commands occupy only one history entry.

Left/Right, Home, End, Backspace, and Delete edit the current line. `Ctrl+C`
cancels the line without terminating the living processor session, while
`Ctrl+L` clears and redraws the terminal. `pwd` and `cd` maintain a Host-side
working directory without changing the RP2350 `flash:/` or `sd:/` namespaces.

`<host-file>` is syntax notation, not a literal `C:\path\hello.txt` filename.

They use the same sequence-bound 64-byte HID ABI and the same single-owner
Device Actor as diagnostic probes and runtime control. `put` writes a hidden temporary
file, verifies the complete CRC32, flushes NOR state, and only then replaces the
target. `get`, `rm`, `mv`, `sync`, processor-side file calls, and `sd:` remain
framework contracts until their backends are integrated.

In interactive mode the runtime row remains above the editable prompt in all
three display modes. It reports workload, clock, cycle count, and RP2350-owned
processor lifecycle state. `quiet`, `status`, and `verbose` change event density
only; they do not turn a workload-specific response into generic liveness.

### Native calculator service

The calculator is the first Host-loaded native workload. Build output contains:

```text
build/workloads/CALC.P86W
```

Load it into the Internal-SRAM execution tier, start it, then use either
`send <expression>` or the explicit `calc` alias:

```text
8086> load build/workloads/CALC.P86W
workload upload: PASS
8086> run
workload run: PASS

8086> send 12+34
[019] CALC 12+34=46  latency=2.2 ms

8086> calc 300 * 200
[040] CALC 300*200=60000  latency=2.6 ms

8086> send 1000/33
[052] CALC 1000/33=30 R10  latency=3.8 ms

8086> stop
workload stop: PASS
8086> restart
workload restart: PASS
```

The Host parses the expression and transports a seven-word request. The
RP2350 dispatches a far call to the selected entry in the uploaded image,
asserts physical `INTR`, and completes the real two-cycle `INTA` handshake.
The Intel 8086 or NEC V30 fetches that code from the Internal-SRAM-backed
processor address, executes `ADD`, `SUB`, unsigned `MUL`, or unsigned `DIV`,
returns to the interrupt handler, publishes the result through the mailbox,
issues EOI, executes `IRET`, and returns to `STI`/`HLT` idle.

Operands are unsigned 16-bit values written in decimal or with Python-style
base prefixes such as `0x1234`. Addition and subtraction use 16-bit wraparound;
multiplication returns the full 32-bit product; division returns quotient and
remainder. Division by zero is rejected by the Host and is not sent to the
physical processor.

This validates bounded Host-loaded Internal-SRAM execution at the manifest's
declared address and entry point. It does not yet claim a general responder for
arbitrary image sizes or unrestricted processor-visible RAM.

For a single-command physical regression, run:

From the Windows checkout, address the package produced by the WSL build:

```powershell
py tools\rp86.py --physical-regression "\\wsl.localhost\Ubuntu-22.04\home\build\github\pi86-rp2350\build\workloads\INVSQRT.P86W"
```

From the WSL checkout itself, use `build/workloads/INVSQRT.P86W`.

The command auto-selects the device, loads and runs the package, and waits for
the firmware-owned structured result. Acceptance requires `COMPLETED`, the
formal PASS flag, and the recorded completion reason; CDC text remains evidence
but cannot decide PASS or FAIL. The command saves the raw CDC log plus session
JSON and returns nonzero on transport, execution, result, or timeout failure.

### Session JSON results

The `rp86.runtime-session/v1` document includes:

- `started`, `finished`, session `passed`, and `failure_reasons`.
- `workload`: the latest observed structured status, with lifecycle, clock,
  cycles, processor flags, completion reason/code, result flags and outcome.
- `workload.image`: source/name and the accepted manifest (image size/CRC32,
  load address, entry CS:IP, stack, shared region and flags). This is `null`
  when the session did not upload that workload; attach never guesses metadata.
- `processor_identity`: the signature, validated processor and
  `firmware_boot_aad16` provenance. Retained identity is not fresh liveness.
- `native_output`: exact result bytes in `hex`, byte length, UTF-8 text with
  replacement decoding, and a separate `truncated` flag. The text contains no
  renderer-added ellipsis. This is the firmware's retained 16-byte result
  field, not the whole stdout stream; the raw CDC log remains available.
- `workload_results`: timestamped terminal observations, including an already
  completed workload found during attach. Repeated identical polling is
  deduplicated; a subsequent run/restart can append another result for the same ID.
- `errors`: timestamped operation failures, including upload record index/count,
  load errors, CPU assertion failures and regression deadline failures.

Numeric CRC/signature/address/flag fields are JSON integers. Only `hex` is a
hexadecimal string. `workload.passed` is `null` for incomplete or unproven work;
fault/timeout is FAIL, while STOPPED is incomplete. Session success does not
mean an arbitrary workload passed: inspect `workload.outcome`, or the stricter
`physical_regression.passed` for the regression acceptance decision. Command
errors remain recorded even when a later retry succeeds.

### Stopped-executor diagnostics

```text
CPU> trace
CPU> trace save fault.json
```

`trace` reads a single diagnostic snapshot, not a full bus-trace export.
It is available after general execution stops, completes, faults or times out.
It never stops a running processor on the user's behalf; running requests are
rejected. `trace on/off` and prepared-responder trace export are not implemented.

The snapshot includes workload ID, processor boot/attempt ID, reason, cycle
count, last observed bus address/type/lanes, valid data, and fault flags.
Unavailable address/data are explicitly marked, not displayed as a fake zero.
For `NO_CYCLE`, the last observed cycle is not necessarily the fault location.
Boot ID zero means the failed attempt never reached processor reset/release.

When a session observes FAULTED/TIMED_OUT, it automatically requests the
snapshot once and retains it in the session JSON `bus_diagnostics` array.
Failed diagnostic reads are recorded in `errors`, without replacing the
original workload outcome. Manual `trace` reads are also retained; `trace save`
additionally writes a Host JSON file (overwriting that named file).

Starting a new execution or accepting a new upload invalidates the old
firmware snapshot; saved Host evidence remains. Another client's state change
can therefore make an old snapshot unavailable. This requires the updated
firmware's `DIAGNOSTICS_REQUEST/RESULT` service; all wire records remain 64 bytes.
See [wire layout](host_protocol.md#11-stopped-executor-diagnostics).

### General Internal-SRAM workload lifecycle

Raw flat binaries default to an automatic clock policy. A workload may request
the general responder explicitly:

```text
8086> load build/workloads/HELLO.P86W
workload upload: PASS
  workload_id=1 state=STAGED detail=164 clock=CLOCK-STEPPED cycles=0
8086> run
workload run: PASS
8086> status
workload status: PASS
  workload_id=1 state=RUNNING detail=164 clock=CLOCK-STEPPED cycles=70
8086> stop
workload stop: PASS
8086> restart
workload restart: PASS
```

Complete syntax is:

```text
load <Host bin|p86w|flash:/file.p86w> [--address N] [--entry CS:IP] [--stack SS:SP]
           [--clock auto|free-running|clock-stepped]
```

A `.p86w` file is one native workload package: the existing 40-byte manifest
followed by the flat processor image. The manifest carries image size, CRC32,
load address, entry point, optional stack, clock mode, and flags. Frequently
used packages can remain on RP-FLASH and be staged without choosing those
values again:

```text
load flash:/CALC.P86W
run
```

The Host reads the package through the RP2350-owned FAT service, validates its
size and CRC32, and uploads the verified image into Internal SRAM. Raw `.bin`
files remain supported and use explicit or default load metadata.

`auto` preserves the prepared calculator path and otherwise selects the safe
general policy. An arbitrary image cannot request FREE_RUNNING unless the
firmware has a prepared current-cycle response profile for it. A general
workload request to switch into FREE_RUNNING is reported as a workload fault;
the processor is never released against an undriven bus. CLOCK_STEPPED
does not mean a fixed wall-clock frequency: every pulse is complete and RP2350
may leave CLK low while servicing memory, I/O, USB, or Host control.

The canonical `hello.bin` writes its identity through port `E9h`, publishes
`IDLE_PREPARE` through the runtime control port, and executes HLT. This explicit
single-use promise is necessary because the fixed minimum-mode HAT does not
route the 8086 `RD`, `WR`, or `DEN` qualifiers. After the remaining valid
instruction fetch, the first non-serviceable indication is accepted as idle,
whether the floating HLT signature appears as no ALE, invalid lanes, or an
unmapped cycle. Without `IDLE_PREPARE`, sustained absence of ALE transitions to
`TIMED_OUT`. `status` reports the actual physical clock and processor-idle flag;
`stop` and `restart` remain available while the processor is idle.

The optional `probe` command exercises the prepared native interrupt responder.
Its `cpu_seq` is not a Host loop counter: it is maintained by the physical
Intel 8086 or NEC V30 and returned only after the processor commits its reply:

```text
processor identity probe: INTEL 8086 cpu_seq=001011 rtt=3.7 ms
```

The 64-byte reply also carries an RP2350-assigned `boot_id` and a reserved
`command_seq` field. The Host request sequence remains in the outer protocol
header for request/reply correlation. These fields have separate meanings:

- `request sequence`: identifies one Host transaction;
- `cpu_seq`: counts completed physical interrupt services;
- `command_seq`: reserved for a future second processor-owned counter; currently zero;
- `boot_id`: separates processor RESET epochs.

`top` describes one physical-CPU environment rather than an operating-system
process list. Its fields include processor lifecycle and clock, active
workload, runtime, PSRAM use, `flash:` and `sd:`
availability, open service handles, I/O counters, interrupt counts, bus errors,
watchdog state, and restart count.

The Host constructs flat native workload manifests, divides images into fixed
64-byte upload records, and exposes `load`, `run`, `stop`, and `restart`
transactions. Canonical firmware accepts begin/data/commit/status records,
validates address, chunk order, and CRC32, and stages images in the 256 KiB
Internal-SRAM processor range. Calculator manifests use the prepared
FREE_RUNNING dispatch path; general manifests use the canonical CLOCK_STEPPED
reset-handoff and bus-service path.
Composite CDC+HID, the accepted 1 MHz bus controller, physical INTR/two-cycle INTA,
the optional prepared-runtime diagnostic probe, command mailbox, status, trace, and Host-directed UF2
entry remain integrated. External PSRAM is a later optional capacity backend
behind the same workload contract.

The first canonical image is `hello.bin`, assembled from
`processor/workloads/examples/hello/hello.asm`. It performs an `AAD 16` identity witness and
prints `HELLO INTEL 8086` or `HELLO NEC V30`. Remaining shell commands are
stable framework contracts whose backends are enabled as their capabilities
are integrated.

### Internal SRAM and shared mailbox

The Host can inspect or change the processor-visible 256 KiB Internal SRAM:

```text
mem read <address> [length]
mem write <address> <byte>...
mem load <Host file> <address>
mem save <address> <length> <Host file>
```

Addresses are physical processor addresses in the current `00000h-3FFFFh`
backing. Transfers use sequence-bound 40-byte HID chunks. For example:

```text
8086> mem write 0x20000 DE AD BE EF
mem write: PASS  4 bytes at 0x20000
8086> mem read 0x20000 4
20000  DE AD BE EF                                      |....|
```

`mailbox <text>` uses the reserved `3F000h-3FFFFh` ownership-transfer mailbox.
The example `shared_mailbox.bin` polls for a Host request, uppercases it on the
physical processor, and returns ownership and the result:

```text
8086> load build/workloads/MAILBOX.P86W
8086> run
8086> mailbox Hello from Host to real 8086
mailbox: PASS generation=1 processor=HELLO FROM HOST TO REAL 8086
```

The final 16-bit `owner` write is the publication point. Polling is deliberate
for this first ABI; no interrupt controller or operating system is required.

## Reading canonical runtime status

The canonical composite CDC+HID firmware exposes its control and observation
plane through CDC:

```powershell
py tools\rp86.py --status --timeout 5
```

The Host automatically selects the CDC interface when exactly one
`VID CAFE / PID 4011` composite device is present. `--port COM27` always
overrides discovery. If no device or multiple devices are present, the Host
does not guess: it reports the candidates and requires `--port` (or a matching
`--hid-serial`) so multiple pi86-rp2350 systems can run concurrently.

## Sharing one physical device between Host clients

The first persistent interactive/monitor process is both the normal shell
and the local Host broker. It is the only process that owns the composite CDC
and HID interfaces. Later processes discover it by USB serial `device_id` and
connect as clients instead of reopening the hardware:

```text
first rp86.py
  = shell + Device Actor + CDC/HID owner + broker

later rp86.py or another rp86_runtime client
  = broker client
```

Reliable 64-byte request/reply transactions use localhost TCP. Read-only
runtime/status snapshots use UDP telemetry. Every remote exchange enters the
Device Actor queue, so the physical HID sequence and current bus transaction
still have exactly one owner. The broker lifecycle is an explicit FSM:

```text
OPENING -> OWNER_ACTIVE -> QUIESCING -> STOPPED
              |
              +-> FAULT -> QUIESCING
```

`--status` automatically queries an active broker when one owns the device, so
it does not contend for the COM port. With one connected board, no selection
argument is needed. With multiple boards or brokers, use `--port COMxx` or
`--hid-serial DEVICE_ID`; the Host refuses ambiguous selection.

Broker termination disconnects clients. This initial contract deliberately
does not migrate hardware ownership to another process automatically; a new
script may become owner after the previous broker releases CDC/HID.

The Host requires one complete `RP86 STATUS BEGIN` / `RP86 STATUS END` block,
so USB startup text cannot be mistaken for the response to a new request. This
operation only observes RP2350 state. Firmware independently initializes the
prepared runtime and captures the physical AAD16 identity at boot. Requests
received during this bounded initialization are handled afterward; there is
no unsolicited bootstrap HID reply. Status, Web, CLI, and physical regression
may connect in any order without triggering initialization or disturbing the
active workload. Retained identity is not a fresh processor liveness proof.

## Restarting or entering the RP2350 UF2 bootloader

Both operations use one sequence-bound 64-byte HID runtime-control record:

```powershell
py tools\rp86.py --reboot
py tools\rp86.py --bootloader
```

Before acknowledging either operation, the RP2350 holds the installed
8086-class processor in RESET, deasserts INTR, stops CLK low, disables the bus
PIO state machines, aborts their DMA channels, and leaves AD high-Z. It then
returns a matching HID runtime-status record. `--reboot` restarts the canonical
firmware; `--bootloader` enters the ROM UF2 bootloader. The expected USB
disconnect after that acknowledgement is a successful transition, not a
transport failure.

HID is the primary control path and does not depend on a healthy CDC stream.
For recovery, the Host falls back to the exact CDC tokens `RP86 REBOOT` and
`RP86 BOOTLOADER` and requires their ACKs. The raw CDC console
also accepts `reboot`, `bootloader`, or `bootsel`. No early control command can
release processor RESET or claim the bus.
