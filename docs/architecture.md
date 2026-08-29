# Host-Managed Bare-Metal Physical Processor Runtime

**Status:** Canonical architecture
**Project:** `pi86-rp2350`

## 1. Definition

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**
>
> *A modern remote-processor runtime for a vintage physical CPU.*

The Intel 8086 or NEC V30 is the processor that executes the workload. The Host manages the
runtime. The RP2350 supplies the resource, supervision, and physical-bus layer
between them.

This is not CPU emulation and it is not a conventional PC architecture.

## 2. Canonical terminology

Names describe hardware and software roles, not the order in which features were
discovered. New source, tests, build targets, protocol documentation, and active
architecture documents use the following vocabulary:

| Legacy or ambiguous term | Canonical term | Rule |
|---|---|---|
| `pi86_` / `PI86_` implementation namespace | `rp86_` / `RP86_` | `Pi86 HAT` remains the proper name of the unmodified hardware baseline |
| V30 bus, `v30_bus` | processor bus, `rp86_processor_bus` | `V30` is used only for NEC-specific behavior or evidence |
| AI Bridge | Host Protocol | ChatGPT, Codex, Python, C, Rust, and Web clients are protocol users, not architectural layers |
| V30 image / V30 ROM helper | processor image | A flat image may execute on either an Intel 8086 or NEC V30; ROM is used only when storage semantics are actually read-only |
| `CONTINUOUS` | `FREE_RUNNING` | Execution Clock Mode in which the clock runs without software-issued per-pulse permission |
| `PACED` | `CLOCK_STEPPED` | Execution Clock Mode in which the RP2350 issues complete clock pulses |
| clock engine | clock mode / clock controller | *Mode* names policy; *controller* names the implementation that supplies clock pulses |
| workload `READY` | workload `STAGED` | Avoids collision with the physical processor `READY` input |
| workload `FAULT` / `TIMEOUT` | `FAULTED` / `TIMED_OUT` | Completed state names use unambiguous past-tense terms |
| heartbeat (architecture) | processor liveness monitoring | `heartbeat` remains a valid CLI and message compatibility term |

Historical validation output, published articles, photographs, and Git history retain
the names that were true when they were produced. Compatibility entry points may also
retain an old filename, but must identify themselves as wrappers around the canonical
RP86 Host runtime. Wire values and workload-state numbers are stable across this naming
migration.

The project keeps three state domains separate:

| Domain | Examples | Meaning |
|---|---|---|
| Runtime State | `IDLE`, `LOADING`, `STAGED`, `RUNNING`, `STOPPED`, `FAULTED` | Host/RP2350 lifecycle |
| Workload State | `EMPTY`, `RECEIVING`, `STAGED`, `RUNNING`, `EXITED`, `FAULTED`, `TIMED_OUT` | One native image lifecycle |
| Execution Clock Mode | `FREE_RUNNING`, `CLOCK_STEPPED` | How complete processor clock pulses are supplied |

## 3. Three fixed roles

```text
Host
= Runtime Controller
  load / run / stdio / files / status / timeout / restart
             |
             v
RP2350
= Companion Resource and Bus Controller
  memory / storage / mailbox / interrupt / clock / reset / PIO / DMA
             |
             v
Intel 8086 / NEC V30
= Bare-Metal Remote Physical Processor
  native x86-class workload execution
```

### Host — Runtime Controller

The Host owns user intent and the interactive runtime experience. It can:

- transfer and select workloads;
- start, stop, inspect, and restart execution;
- provide stdin/stdout and file operations;
- read status, trace, results, and fault information;
- apply timeouts or optional restart policy.

The first client is Python. Other languages, Web tools, ChatGPT/Codex, or other
automation may use the same protocol. No particular client is part of the V30
machine.

### RP2350 — Companion Resource and Bus Controller

The RP2350 is the sole low-level owner of:

- the multiplexed processor bus, clock, reset, and interrupt signaling;
- PIO and DMA state;
- Internal SRAM allocation used by the runtime;
- External PSRAM access and allocation;
- External NOR Flash, SD Card, and filesystem metadata;
- mailbox, stdio, trace, and Host transport state.

It validates and serializes Host and processor requests. It is not an x86 CPU and it
does not execute the processor workload.

### Intel 8086 / NEC V30 — Bare-Metal Remote Physical Processor

The installed physical processor:

- fetches and executes native instructions;
- owns architectural registers and control flow;
- uses assigned code, data, stack, heap, and shared-memory regions;
- requests runtime services through defined mailbox/I/O/interrupt mechanisms;
- may exit, fault, hang, or time out like any real bare-metal processor.

The term *remote processor* means a processor loaded and supervised by another
computer. It does not imply that the 8086 or V30 is emulated or connected through a
network.

## 4. Runtime model

```text
Load -> Run -> Communicate -> Observe
                              |
                       Exit / Fault / Timeout
                              |
                           Restart
```

The Host acts as the operating environment around the V30 rather than placing a
general operating system on it. The V30 receives only the small runtime services
required by its workload.

BIOS, DOS, ELKS, boot sectors, PC memory maps, and 825x-compatible services are
optional programs or experiments. They are not architectural prerequisites.

## 5. Workload and launch

The initial transfer form is a flat native 8086-class binary plus explicit launch
metadata:

```text
image          native 8086-class machine code
load_address   V30 physical address
entry          initial CS:IP
stack          initial SS:SP
segments       initial DS and ES when required
```

ELF may be retained by development tools for symbols and relocation, while the
physical transfer uses a flat image. The RP2350 loads assigned processor-visible
memory and provides a minimal reset handoff at `FFFF0h`; a BIOS is not required.

## 6. Resource and ownership model

> **Host and V30 share content, but they do not share low-level ownership.**

```text
Host request -----------+
                        v
                  RP2350 owner
                 /      |      \
             PSRAM   FAT volumes  bus/runtime state
                        ^
processor service request ----+
```

| Resource | Intended primary role | Implementation / validation status |
|---|---|---|
| RP2350 Internal SRAM | firmware/realtime state plus the first workload-execution, CPU-visible RAM, and shared-memory tier | 256 KiB processor range (`00000h-3FFFFh`) is reserved; Host HID staging and 1 MHz bounded execution are validated; general branch/loop/stack/RAM/I/O execution is physically validated with the CLOCK_STEPPED controller |
| External PSRAM | optional capacity tier for larger workloads, bulk shared memory, snapshots, and cache/refill backing | SDK-backed detection/access framework implemented; direct/general processor execution is not physically validated |
| External NOR Flash | first 4 MiB reserved for firmware; final 12 MiB is shared `flash:` | FAT16 `RP-FLASH` mount, persistence, and media self-test physically validated; Host `ls`, `df`, `cat`, and atomic `put` are implemented; processor file services and remaining mutations remain open |
| SD Card | optional removable `sd:` FAT volume | GPIO safe-state initialization implemented; card/FAT service not implemented |

The RP2350 owns the controllers and filesystem metadata. The Host and V30 access
assigned content through RP2350 services.

Stable public paths use semantic volume names:

```text
flash:/hello.bin
flash:/output.txt
sd:/datasets/input.dat
sd:/traces/run001.log
```

## 7. Runtime states

```text
EMPTY -> STAGED -> RUNNING -> EXITED
                    |  |
                    |  `-> FAULTED / TIMED_OUT
                    |              |
                    `--------------+-> STOPPED -> RESTART or LOAD
```

- **EMPTY:** no selected workload; storage and Host control remain available.
- **STAGED/STOPPED:** Host may prepare memory and launch state.
- **RUNNING:** processor owns workload execution; Host observes and uses approved
  mailbox/shared regions.
- **EXITED:** normal workload completion with retained results.
- **FAULTED/TIMED_OUT:** abnormal workload result with retained evidence and Host
  control still alive.

A crash is a valid workload outcome. It is not rejected in advance. The Host
reports it and the user may inspect or restart the V30.

## 8. Processor runtime services

The baseline service surface is deliberately small:

- stdin/read and stdout/write;
- file open/read/write/seek/close;
- workload exit/result;
- liveness/status response;
- optional shared-memory notification.

The processor never directly controls USB, FAT, NOR Flash, SD, PSRAM, PIO, or DMA.

## 9. Physical timing boundary

The original Pi86 HAT keeps processor `READY` asserted. The runtime therefore
controls latency through the processor clock rather than adding READY wait states.
It has two Execution Clock Modes:

| Mode | Clock policy | Intended use |
|---|---|---|
| **FREE_RUNNING** | continuously running measured clock; PIO/DMA and prepared state meet each bus deadline | high-throughput prepared workloads, liveness monitoring, interrupts, and bounded services |
| **CLOCK_STEPPED** | RP2350 issues complete clock pulses; the clock may remain low between pulses while M33 services memory or I/O | general Internal-SRAM execution, bring-up, inspection, and slow or variable-latency services |

Bounded Host-loaded execution is physically accepted for the calculator image
at `10000h`, including manifest/CRC validation, lifecycle control, physical
fetch, interrupt return, and mailbox result.

The CLOCK_STEPPED controller is integrated into the canonical Host lifecycle.
The Host uploads a flat image and clock metadata, RP2350 constructs the
`FFFF0h` reset handoff, and the physical processor executes from the 256 KiB
Internal-SRAM tier. `status` reports the actual clock mode and serviced cycle
count; `stop` asserts RESET only after CLK is parked low; `restart` reconstructs
the handoff and begins a new physical execution epoch.

Runtime switching between FREE_RUNNING and CLOCK_STEPPED is a cooperative
operation, not a mid-cycle clock change. It is available only to a prepared
workload whose PIO/DMA response profile remains active under both modes. A
general Internal-SRAM workload has only the CLOCK_STEPPED responder; a request
to enter FREE_RUNNING is therefore rejected as a retained workload fault rather
than allowing the processor to execute against an undriven bus. Both clock
controllers, the prepared request/commit transition, and canonical
CLOCK_STEPPED lifecycle control have been physically validated.

The fixed HAT does not route the 8086 minimum-mode `RD`, `WR`, or `DEN`
qualifiers. Intel specifies that software HLT emits one ALE without a
qualifying bus-control signal; the visible subset alone can therefore resemble
an I/O-read indication, invalid byte lanes, or no usable cycle. A native
workload writes `IDLE_PREPARE` to the runtime control port immediately before
HLT. This creates a single-use promise: after the remaining valid instruction
fetch, the first non-serviceable indication is accepted as idle. Without that
promise, lack of ALE reaches a bounded `TIMED_OUT` state; ordinary unmapped I/O
remains a retained fault.

Host software, USB, and filesystem work still do not participate directly in a
partially completed bus cycle. CLOCK_STEPPED mode pauses between complete pulses; it does
not stretch or truncate a pulse. External PSRAM remains an optional later capacity
tier whose access policy must be measured on hardware.

## 10. Host Protocol and shell

The Host Protocol contains typed operations, sequence-bound completion, explicit
errors, capability reporting, and chunked/bulk transfer where needed. The shell
is one user interface over that protocol, not the wire contract itself.

The command framework includes:

```text
load  run  stop  restart
send  stdin  stdout  console
ls  cat  put  get  rm  mv  df  sync
mem read/write/load/save
status  top  info  trace  regs
ping  timeout  quiet  verbose  quit
```

An unimplemented backend reports `NOT AVAILABLE`; it never fabricates success.

## 11. Failure boundary

The platform guarantees resource ownership and electrical safety, not workload
success. On a workload fault or timeout it should:

1. preserve available status, memory, and trace;
2. keep Host control and stdio infrastructure alive;
3. report the observed outcome;
4. wait for explicit inspection, restart, stop, or load.

If the runtime itself loses bus ownership or corrupts critical PIO/DMA state,
the RP2350 enters an electrical safe state and retains diagnostics.

## 12. Project boundary

The project remains intentionally narrower than a PC clone or V30 operating
system. It builds the modern runtime needed to load, communicate with, observe,
and restart a real vintage CPU while preserving native execution.

The architecture can be summarized as:

> **Host controls the runtime. RP2350 owns resources and the physical bus. The
> Intel 8086 or NEC V30 owns native execution.**

Or operationally:

> **Load. Run. Talk. Watch. Restart.**

## 13. Detailed contracts

- [`host_runtime_architecture.md`](host_runtime_architecture.md) — detailed runtime, permission, and implementation contract
- [`host_runtime_shell.md`](host_runtime_shell.md) — shell command surface
- [`memory_architecture.md`](memory_architecture.md) — memory and storage ownership
- [`host_protocol.md`](host_protocol.md) — Host/RP2350 wire semantics
- [`hardware.md`](hardware.md) — board resources, signal mapping, and physical ownership
- [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — current architecture decision
