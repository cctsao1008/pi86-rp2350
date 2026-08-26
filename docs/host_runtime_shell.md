# RP86 Host Runtime Shell

The Host declares the installed processor with `--processor`, because the
8086-class interface has no CPUID instruction. The canonical native runtime
independently executes a branch-free `AAD 16` discriminator and returns the
result in every committed 64-byte witness. A declaration of `intel-8086` or
`nec-v30` is accepted only when it matches that physical processor evidence.

**RP86** is the Host runtime and small remote shell for a real Intel 8086 or NEC V30. It is
not an operating system running on the processor. The Host provides control and
runtime services; the RP2350 owns resources and the physical bus; the processor executes
bare-metal native workloads.

The shell is the reference interface to the
**Host-Managed Bare-Metal Physical Processor Runtime**. It is not the wire
protocol and it is not required to be implemented in Python forever.

`RPBridge` is the narrower transport layer beneath RP86: composite CDC/HID,
the 64-byte ABI, and the local multi-client broker. `v30bridge.py` is retained
only as a backward-compatible command name.

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
metadata, PSRAM allocator, or bus-engine state.

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
| Console | `send`, `console`, `stdin`, `stdout` |
| Files | `ls`, `cat`, `put`, `get`, `rm`, `mv` |
| Storage | `df`, `mount`, `unmount`, `sync` |
| Memory | `mem read`, `mem write`, `mem load`, `mem save` |
| Observation | `status`, `top`, `info`, `trace`, `regs` |
| Supervision | `ping`, `timeout`, heartbeat, restart, `bootloader` |
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
the persistent heartbeat prompt. This line editor is implemented on both
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
Device Actor as heartbeat and runtime control. `put` writes a hidden temporary
file, verifies the complete CRC32, flushes NOR state, and only then replaces the
target. `get`, `rm`, `mv`, `sync`, processor-side file calls, and `sd:` remain
framework contracts until their backends are integrated.

In interactive mode the physical-processor `ALIVE` row remains above the
editable prompt in all three display modes. `quiet`, `status`, and `verbose`
change event density only; they do not hide liveness.

The displayed `cpu_seq` is not a Host loop counter. It is maintained by the
physical Intel 8086 or NEC V30 inside the native interrupt service routine and
returned only after the processor commits its reply:

```text
| ● INTEL 8086 ALIVE  cpu_seq=001011  rtt=3.7 ms  lost=0
```

The 64-byte reply also carries an RP2350-assigned `boot_id` and a reserved
`command_seq` field. The Host request sequence remains in the outer protocol
header for request/reply correlation. These fields have separate meanings:

- `request sequence`: identifies one Host transaction;
- `cpu_seq`: counts completed physical interrupt services;
- `command_seq`: reserved for a future second processor-owned counter; currently zero;
- `boot_id`: separates processor RESET epochs.

`top` describes one physical-CPU environment rather than an operating-system
process list. Its eventual fields include processor liveness and clock, active
workload, runtime, heartbeat latency/loss, PSRAM use, `flash:` and `sd:`
availability, open service handles, I/O counters, interrupt counts, bus errors,
watchdog state, and restart count.

The Host constructs and validates flat native workload manifests, divides
images into fixed 64-byte upload records, and exposes `load`, `run`, `stop`, and
`restart` transactions. Canonical firmware now accepts begin/data/commit/status
records and stages valid images in a 256 KiB Internal-SRAM processor range with
address, ordered-chunk, and CRC32 validation. Composite CDC+HID, the accepted
1 MHz companion bus engine, physical INTR/two-cycle INTA, persistent heartbeat,
command mailbox, status, trace, and Host-directed UF2 entry remain integrated.

`run`, `stop`, and `restart` still report service unavailable because the
general arbitrary-address Internal-SRAM PIO/DMA responder is not integrated.
Staging therefore cannot be mistaken for native launch. External PSRAM remains
a later optional capacity backend behind the same workload contract.

The first canonical image is `hello.bin`, assembled from
`firmware/workloads/hello.asm`. It performs an `AAD 16` identity witness and
prints `HELLO INTEL 8086` or `HELLO NEC V30`. Remaining shell commands are
stable framework contracts whose backends are enabled as their capabilities
are integrated.

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

The first persistent interactive/heartbeat process is both the normal shell
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
heartbeat/status snapshots use UDP telemetry. Every remote exchange enters the
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

The Host requires one complete `PI86 STATUS BEGIN` / `PI86 STATUS END` block,
so USB startup text cannot be mistaken for the response to a new request. This
operation only observes RP2350 state. Before the first HID record it reports
`IDLE` and leaves the processor in RESET. After startup it reports `RUNNING`
without disturbing the active physical bus or heartbeat runtime.

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
For compatibility and recovery, the Host falls back to the exact CDC tokens
`PI86 REBOOT` and `PI86 BOOTLOADER` and requires their ACKs. The raw CDC console
also accepts `reboot`, `bootloader`, or `bootsel`. No early control command can
release processor RESET or claim the bus.
