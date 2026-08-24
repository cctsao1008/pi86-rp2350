# PC1-C1 Native BIOS Execution Platform

> **Archive status: SUPERSEDED OR COMPLETED.** This document is retained for
> engineering provenance and is not current architecture or an active plan. See
> the [documentation archive index](../README.md) for its replacement authority.

## Objective

PC1-C1 turns the accepted V30 bus engine into a small, deterministic execution
platform. A single UF2 shall load versioned V30 images from RP2350 flash into
internal-SRAM-backed memory, execute them through the current-address PIO/DMA
path, and expose a human-facing USB CDC console without placing USB or the M33
in a current V30 bus-cycle deadline.

The first integrated acceptance message is:

```text
PI86 BIOS v0.1
ROM PASS
RAM PASS
INT 10H PASS
CDC PASS
BOOT PASS

V30>
```

This is an aggressive functional target, not permission to weaken bus
ownership. Any unsupported or late response remains high-Z, and every run ends
with RESET high, CLK low, and AD high-Z.

## Initial physical memory map

| Physical range | Initial role |
|---|---|
| `00000h-003FFh` | interrupt vector table |
| `00400h-004FFh` | BIOS data and test status |
| `00500h-07FFFh` | internal-SRAM-backed conventional RAM and stacks |
| `08000h-0FFFFh` | loadable test-program RAM |
| `F0000h-FFFFFh` | Native BIOS ROM window |
| `FFFF0h` | architectural reset-vector entry |

The first implementation may expose only a declared subset of these ranges.
An address is supported only after its read, write, byte-lane, and worst-case
latency behavior has physical evidence.

## Flash image manifest

V30 programs live persistently in RP2350 flash but are copied and validated
before RESET release. XIP flash, USB, and M33 policy code are never consulted
to complete a current V30 read cycle.

Every image record carries at least:

```text
format version
image name
load physical address
entry CS:IP
image length
required RAM
CRC32 or SHA-256
expected completion code
execution timeout
```

The companion keeps RESET asserted if the manifest, bounds, or checksum is
invalid. The first embedded suite contains `hello`, `int10`, `stack`, and
`cpu` programs. Later CDC upload uses the same record and validation rules.

## Companion console ABI

The accepted `00E9h` byte-write contract remains permanent. PC1-C1 extends the
namespace only as each direction is independently validated:

| Port | Direction | Contract |
|---|---|---|
| `00E9h` | V30 -> companion | raw console/diagnostic byte write |
| `00E9h` | companion -> V30 | next console byte read; PC1-C1 gated |
| `00EAh` | companion -> V30 | status: bit 0 RX ready, bit 1 TX ready, bit 7 host connected |
| `00EAh` | V30 -> companion | reserved console control |
| `00E8h` | V30 -> companion | structured test completion code |

An input read is legal only after the BIOS observes RX-ready. With READY tied
high on the golden HAT, an empty read cannot wait for the M33 or USB host; it
must be avoided by polling and must never return stale data.

The first BIOS-facing software interrupt is `INT 10h`, function `AH=0Eh`.
Its teletype backend writes `AL` to `00E9h`. The test installs an IVT vector,
uses a real RAM stack, calls the handler repeatedly, and returns with `IRET`.
External INTR/NMI/INTAK behavior is a later independently gated milestone.

## USB CDC separation

The intended composite USB layout is:

```text
CDC0  V30 console and monitor
CDC1  RP2350 engineering log and retained bus evidence
```

CDC0 is human-facing and remains concise. CDC1 carries PIO/DMA counters,
deadline results, and traces. Until a two-interface TinyUSB descriptor is
implemented, the single existing stdio CDC may time-multiplex these roles, but
the output must retain explicit `[V30 CONSOLE]` and `[ENGINEERING]` framing.

The realtime bridge is buffered:

```text
V30 I/O cycle <-> PIO/DMA <-> internal-SRAM ring <-> M33 <-> USB CDC
```

USB callbacks never drive GPIO and never participate in the current-cycle
address-to-data decision.

## Implementation gates

### C0C1-B1: bounded multi-cycle ROM engine

- replay one identical, address-independent table for every physical cycle;
- select each response from the current early-T1 key;
- use a PIO-local table sentinel for explicit miss/high-Z handling;
- reset lookup state at every ASTB cycle;
- execute forward, backward, repeated, and taken-branch fetches;
- keep the M33 out of the current-cycle path.

The first table is limited to the physically accepted 32-entry scan depth. A
finite execution budget may repeat that same table in SRAM; the table content
must not encode or predict fetch order.

### C0C1-B2: RAM and software INT

- add bounded internal-SRAM-backed word and byte reads/writes;
- initialize SS:SP and prove PUSH/POP plus nested CALL/RET;
- install and fetch an IVT entry below `00400h`;
- execute repeated `INT 10h/AH=0Eh` and `IRET` paths.

The 2026-08-20 B2-A physical baseline passed a deliberately bounded two-epoch
form of this gate. Epoch A learned the V30's real `000C/F000/F046` INT stack
writes; Epoch B replayed them through the current-address table and completed
`IRET` to the BIOS checkpoint. This proves the physical IVT, stack transaction,
and IRET path, but not general or same-run coherent RAM. See
[`validation/pc1c0c1b2a_int10_stack_validation.md`](../../validation/pc1c0c1b2a_int10_stack_validation.md).

The 2026-08-20 B2-B physical baseline then passed same-run coherence for one
live word at physical `00100h`. PIO1 captured the V30's `1234h` write at R2,
retained the raw scattered-GPIO value locally, and replayed it on the later
read without a current-cycle M33 round trip. The V30 proved consumption by
writing the value it read to `00102h`; the passive observer recorded `1234h`
at all three cycles. This remains a learned 30-pair execution path and a
single RAM slot, not general RAM. See
[`validation/pc1c0c1b2b_same_run_ram_validation.md`](../../validation/pc1c0c1b2b_same_run_ram_validation.md).

### PC1-C1 loader and interactive console

- validate and copy embedded flash images before RESET release;
- run the four-program suite with structured `00E8h` completion;
- bridge `00E9h` output live to CDC0;
- prove polled CDC RX status/data with an echo command;
- add `HELP`, `INFO`, `D`, `MW`, `RUN`, and `RESET` monitor commands;
- preserve the descriptor-fed C0C0 and C0C0-H targets as regressions.

## Safety and failure policy

On timeout, unsupported execution, response deadline miss, ownership error, or
bad image identity, the supervisor asserts RESET, parks CLK low, releases AD,
and retains the final bus cycles. A clean text message is never allowed to
override a failed electrical or timing gate.
