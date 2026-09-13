# IA16 Binary Execution Laboratory

Status: **planned** under GitHub Issue #80.

## Purpose

The IA16 Binary Execution Laboratory is a small host-side engineering tool for executing the same IA16 machine-code artifacts that are intended for the physical RP86 processor path.

Its purpose is diagnostic, not architectural substitution:

> Execute the production binary under a highly observable software CPU model so instruction, register, segment, and memory behavior can be examined before final physical Intel 8086 validation.

The laboratory is deliberately narrower than an emulator product, RP86 simulator, or digital twin.

## Position in the development flow

```text
FreeRTOS / processor source
        ↓
Open Watcom / NASM / WLINK
        ↓
production .P86W
        ↓
┌───────────────────────────────┐
│ IA16 Binary Execution Lab     │
│                               │
│ x86-16 execution              │
│ register/segment visibility   │
│ memory read/write tracing     │
│ symbol-aware diagnostics      │
└───────────────┬───────────────┘
                ↓
        machine-level evidence
                ↓
      physical Intel 8086
        final acceptance
```

The physical Intel 8086 remains the final architectural authority. The laboratory exists to reduce the amount of exploratory debugging that must be performed on hardware.

## Core design rules

### Execute the production artifact

The laboratory consumes the normal RP86 `.P86W` artifact or its linked IA16 image. It must not introduce a separately compiled simulation build.

This preserves the exact compiler and linker behavior being investigated, including:

- Open Watcom 16-bit code generation;
- NASM-generated port/startup code;
- WLINK layout and relocation decisions;
- the selected C memory model and calling convention;
- actual FreeRTOS kernel and workload machine code.

The existing `.P86W` decoder is the source of truth for package parsing.

### Do not reimplement FreeRTOS

The laboratory executes the real FreeRTOS binary. It must not contain a second scheduler, queue, semaphore, or task-state implementation.

Reference code may describe expected invariants such as list membership, pointer reciprocity, or item counts. Those invariants are assertions against the executed binary, not a substitute RTOS.

### Separate ISA legality from execution behavior

An x86 execution engine operating in 16-bit mode is not proof that the image uses only instructions available on an original Intel 8086.

Therefore two validation concerns remain distinct:

```text
strict Intel 8086 ISA validation
            +
binary execution and state tracing
```

The laboratory answers the second question. ISA-baseline validation remains independent.

### Do not model the RP2350 implementation

The laboratory represents processor-visible software execution only. It does not model:

- PIO state machines;
- DMA;
- GPIO electrical behavior;
- USB;
- READY timing;
- INTA bus waveforms;
- RP2350 core scheduling;
- cycle-accurate bus timing.

If future tests require processor-visible I/O or interrupt behavior, only the minimum architectural contract required by the binary should be modeled.

## Initial architecture

```text
                         production .P86W
                                │
                                ▼
                         workload decoder
                                │
                 ┌──────────────┴──────────────┐
                 │                             │
                 ▼                             ▼
              image                        manifest
                 │                  load / entry / stack
                 └──────────────┬──────────────┘
                                ▼
                         IA16 Machine
                    ┌───────────┼───────────┐
                    │           │           │
                    ▼           ▼           ▼
                 memory      registers     engine
                    │           │           │
                    └───────────┼───────────┘
                                ▼
                         Observability
                    ┌───────────┼───────────┐
                    │           │           │
                    ▼           ▼           ▼
              instruction     memory      register
                 trace         hooks       snapshot
                    │           │           │
                    └───────────┼───────────┘
                                ▼
                           symbol map
                                │
                                ▼
                         targeted fixture
                                │
                                ▼
                      invariant assertions
```

## Repository placement

The laboratory remains an engineering tool, not Host production runtime:

```text
tools/
└── ia16_lab/
    ├── __init__.py
    ├── loader.py
    ├── machine.py
    ├── symbols.py
    ├── trace.py
    └── cli.py

tests/
└── tools/
    └── ia16_lab/
        ├── fixtures/
        │   └── freertos_block_state.py
        ├── test_machine.py
        └── test_freertos_block_path.py
```

The project-level naming uses **laboratory** rather than simulator, emulator, or digital twin to make the scope boundary explicit.

## Three-step implementation boundary

### Step 1: load and execute a real RP86 binary

The first step provides only the minimum execution path:

```text
.P86W
  ↓
existing decoder
  ↓
map IA16 image
  ↓
initialize manifest CS:IP and SS:SP
  ↓
execute
```

The model should initially map the processor-visible memory required by the target workload. For the current baseline, the validated 256 KiB Internal-SRAM window is sufficient.

Registers that production startup code is responsible for establishing, such as DS or ES, should not be silently fixed by the laboratory merely to make execution succeed.

A library-style x86-16 execution engine is preferred. Unicorn is the initial candidate because it supports scripted register and memory control plus execution hooks, but the laboratory architecture should keep the backend replaceable.

### Step 2: expose machine-level behavior

The second step adds only the observability needed for generated-code analysis:

- instruction address tracing;
- memory reads;
- memory writes;
- selected register and segment snapshots;
- WLINK symbol-to-address mapping;
- watch filtering for selected addresses, ranges, or symbols.

A useful diagnostic record should answer:

```text
which instruction executed?
which function/symbol was it in?
what were CS:IP, DS, SS, SP and relevant GPRs?
which effective address was read or written?
what value changed?
```

Trace output must remain selective. Full instruction and memory logging is not the default because it would obscure the small region under investigation.

Production linkage should not be changed merely to improve laboratory symbol visibility. In particular, static kernel routines should remain static.

### Step 3: FreeRTOS #75 vertical slice

The first application is the current FreeRTOS indefinite-block investigation around:

```text
prvAddCurrentTaskToDelayedList()
    ↓
uxListRemove()
    ↓
listINSERT_END(&xSuspendedTaskList, ...)
```

The fixture should construct only the minimum linked-memory state needed to execute that path:

```text
pxCurrentTCB
    ↓
Consumer TCB
    └── xStateListItem

ready list
    └── Consumer

xSuspendedTaskList
    └── empty
```

The established RP86 FreeRTOS runtime invariant applies:

```text
SS = DS = DGROUP
```

The fixture must follow the actual compiled/list structure layout. It must not invent a parallel representation and then translate it into the binary.

The expected state transition is expressed as invariants rather than a second scheduler implementation.

Before:

```text
ready-list count = 1
Consumer.state-item.container = ready list
suspended-list count = 0
```

After indefinite blocking:

```text
ready-list count = 0
suspended-list count = 1
Consumer.state-item.container = suspended list
```

Structural integrity must also hold:

```text
item.next.previous == item
item.previous.next == item
list.end.next.previous == list.end
list.end.previous.next == list.end
pvOwner unchanged
```

A PASS means the real compiled binary performs the expected mutation and moves the #75 investigation outward toward context-switch, interrupt, or integration behavior.

A FAIL is equally useful if the laboratory identifies the first machine-level divergence, including the instruction address, symbol offset, register/segment state, effective memory address, and unexpected mutation.

## Deferred capabilities

The initial laboratory does not require interrupt or I/O emulation.

Capabilities such as the following are added only when a concrete test requires them:

```text
INT / IRET
IN / OUT
periodic tick injection
RP86 processor-visible I/O semantics
```

The existence of an extension point is sufficient; unused hardware behavior should not be implemented speculatively.

## Non-goals

The laboratory is not intended to become any of the following without a new architectural decision:

```text
full RP86 simulator
cycle-accurate Intel 8086 emulator
8086 prefetch-queue model
bus T-state simulator
RP2350 simulator
PIO/DMA/GPIO simulator
PC/XT emulator
BIOS or DOS environment
ELKS platform model
PSRAM model
NEC V20/V30 extension emulator
FreeRTOS semantic reimplementation
```

## Evidence hierarchy

The laboratory adds a new diagnostic layer but does not change the authority hierarchy:

```text
source / ABI review
        ↓
strict ISA validation
        ↓
IA16 binary execution evidence
        ↓
physical Intel 8086 evidence
```

Software execution can falsify many hypotheses cheaply. Physical silicon remains the final acceptance point for behavior that depends on the real processor, RP2350 integration, interrupt/bus timing, or hardware-specific effects.

## Expansion rule

The laboratory should remain small unless the initial FreeRTOS vertical slice demonstrates clear diagnostic leverage.

The implementation should be expanded only if it can materially improve questions such as:

```text
Which exact instruction diverged?
Which register or segment carried the wrong value?
Which effective address was accessed?
Which TCB/list field was first corrupted?
```

If it cannot provide substantially better evidence than disassembly plus existing physical telemetry, it should remain a small compiler/ABI laboratory rather than grow into a larger simulation subsystem.

## Related work

- GitHub Issue #80 — implementation and go/no-go tracking
- GitHub Issue #75 — first FreeRTOS vertical slice
- GitHub Issue #79 — strict Intel 8086 opcode-baseline validation
- [`docs/reference/processor_c16_abi.md`](../reference/processor_c16_abi.md) — Open Watcom C/16 ABI contract
- [`docs/reference/processor_freertos_8086_port.md`](../reference/processor_freertos_8086_port.md) — FreeRTOS 8086 portable-layer contract
