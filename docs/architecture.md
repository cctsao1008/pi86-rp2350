# Host-Managed Bare-Metal Physical Processor Runtime

**Status:** Canonical architecture
**Project:** `pi86-rp2350`

## 1. Definition

> **pi86-rp2350 is a host-managed bare-metal processor runtime for a real NEC V30.**
>
> *A modern remote-processor runtime for a vintage physical CPU.*

The V30 is the processor that executes the workload. The Host manages the
runtime. The RP2350 supplies the resource, supervision, and physical-bus layer
between them.

This is not CPU emulation and it is not a conventional PC architecture.

## 2. Three fixed roles

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
NEC V30
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

- the multiplexed V30 bus, clock, reset, and interrupt signaling;
- PIO and DMA state;
- Internal SRAM allocation used by the runtime;
- External PSRAM access and allocation;
- External NOR Flash, SD Card, and filesystem metadata;
- mailbox, stdio, trace, and Host transport state.

It validates and serializes Host and V30 requests. It is not an x86 CPU and it
does not execute the V30 workload.

### NEC V30 — Bare-Metal Remote Physical Processor

The physical NEC V30:

- fetches and executes native instructions;
- owns architectural registers and control flow;
- uses assigned code, data, stack, heap, and shared-memory regions;
- requests runtime services through defined mailbox/I/O/interrupt mechanisms;
- may exit, fault, hang, or time out like any real bare-metal processor.

The term *remote processor* means a processor loaded and supervised by another
computer. It does not imply that the V30 is emulated or connected through a
network.

## 3. Runtime model

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

## 4. Workload and launch

The initial transfer form is a flat native V30 binary plus explicit launch
metadata:

```text
image          native V30 machine code
load_address   V30 physical address
entry          initial CS:IP
stack          initial SS:SP
segments       initial DS and ES when required
```

ELF may be retained by development tools for symbols and relocation, while the
physical transfer uses a flat image. The RP2350 loads assigned V30-visible
memory and provides a minimal reset handoff at `FFFF0h`; a BIOS is not required.

## 5. Resource and ownership model

> **Host and V30 share content, but they do not share low-level ownership.**

```text
Host request -----------+
                        v
                  RP2350 owner
                 /      |      \
             PSRAM   FAT volumes  bus/runtime state
                        ^
V30 service request ----+
```

| Resource | Intended primary role | Implementation / validation status |
|---|---|---|
| RP2350 Internal SRAM | firmware, PIO/DMA state, mailbox, prepared/cache windows, short trace | available; firmware use implemented; selected V30-visible paths physically validated in retained targets |
| External PSRAM | principal V30 execution memory and Host/V30 shared volatile workspace | SDK-backed detection/access framework implemented; arbitrary V30 execution not physically validated |
| External NOR Flash | firmware/reserved region plus shared `flash:` FAT volume | 16 MB device and firmware storage available; shared FAT volume not implemented |
| SD Card | optional removable `sd:` FAT volume | GPIO safe-state initialization implemented; card/FAT service not implemented |

The RP2350 owns the controllers and filesystem metadata. The Host and V30 access
assigned content through RP2350 services.

Stable public paths use semantic volume names:

```text
flash:/workloads/hello.bin
flash:/results/output.txt
sd:/datasets/input.dat
sd:/traces/run001.log
```

## 6. Runtime states

```text
EMPTY -> LOADED -> RUNNING -> EXITED
                    |  |
                    |  `-> FAULT / TIMEOUT
                    |              |
                    `--------------+-> STOPPED -> RESTART or LOAD
```

- **EMPTY:** no selected workload; storage and Host control remain available.
- **LOADED/STOPPED:** Host may prepare memory and launch state.
- **RUNNING:** V30 owns workload execution; Host observes and uses approved
  mailbox/shared regions.
- **EXITED:** normal workload completion with retained results.
- **FAULT/TIMEOUT:** abnormal workload result with retained evidence and Host
  control still alive.

A crash is a valid workload outcome. It is not rejected in advance. The Host
reports it and the user may inspect or restart the V30.

## 7. V30 runtime services

The baseline service surface is deliberately small:

- stdin/read and stdout/write;
- file open/read/write/seek/close;
- workload exit/result;
- heartbeat/status response;
- optional shared-memory notification.

The V30 never directly controls USB, FAT, NOR Flash, SD, PSRAM, PIO, or DMA.

## 8. Physical timing boundary

The original Pi86 HAT keeps V30 `READY` asserted. An active bus cycle therefore
cannot wait for Host software, USB, a filesystem operation, or arbitrary storage
latency.

PIO/DMA and prepared RP2350 state own timing-critical bus behavior. M33 firmware
prepares and supervises future state; it does not perform an unbounded
current-cycle lookup.

General PSRAM-backed arbitrary execution remains a physical implementation gate.
It requires a measured bounded response mechanism on the existing fixed-`READY`
HAT, such as a validated prepared or staged hit path. Architectural documents
must not describe this gate as already complete.

## 9. Host Protocol and shell

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

## 10. Failure boundary

The platform guarantees resource ownership and electrical safety, not workload
success. On a workload fault or timeout it should:

1. preserve available status, memory, and trace;
2. keep Host control and stdio infrastructure alive;
3. report the observed outcome;
4. wait for explicit inspection, restart, stop, or load.

If the runtime itself loses bus ownership or corrupts critical PIO/DMA state,
the RP2350 enters an electrical safe state and retains diagnostics.

## 11. Project boundary

The project remains intentionally narrower than a PC clone or V30 operating
system. It builds the modern runtime needed to load, communicate with, observe,
and restart a real vintage CPU while preserving native execution.

The architecture can be summarized as:

> **Host controls the runtime. RP2350 owns resources and the physical bus. The
> NEC V30 owns native execution.**

Or operationally:

> **Load. Run. Talk. Watch. Restart.**

## 12. Detailed contracts

- [`host_runtime_architecture.md`](host_runtime_architecture.md) — detailed runtime, permission, and implementation contract
- [`host_runtime_shell.md`](host_runtime_shell.md) — shell command surface
- [`memory_architecture.md`](memory_architecture.md) — memory and storage ownership
- [`host_protocol.md`](host_protocol.md) — Host/RP2350 wire semantics
- [`dual_core_partitioning.md`](dual_core_partitioning.md) — RP2350 realtime/service ownership
- [`hardware_contract.md`](hardware_contract.md) — current physical interface
- [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — current architecture decision
