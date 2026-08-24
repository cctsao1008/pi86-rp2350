# Memory Architecture

## 1. Scope

This document defines the canonical memory terminology and minimum memory contracts for `pi86-rp2350`.

Two concepts must remain separate:

> **The V30 Memory Map describes CPU-visible semantics. Backing Resources describe implementation.**

A V30 address range is therefore not synonymous with RP2350 SRAM, PSRAM, Flash, or any other physical device.

## 2. Canonical terminology

Use these names consistently in code and canonical documentation:

| Term | Meaning |
|---|---|
| **RP2350 Internal SRAM** | SRAM physically inside the RP2350 |
| **External NOR Flash** | non-volatile Flash outside the RP2350 die; 16 MB on the current RP2350-PiZero |
| **External PSRAM** | external volatile bulk memory used by the target machine configuration |
| **SD Card** | optional removable bulk storage |
| **V30 Memory Map** | the NEC V30 20-bit CPU-visible address-space semantics |
| **Backing Resource** | the physical or prepared resource used to materialize a mapped V30 region |
| **Deterministic V30 Window** | explicitly prepared on-chip state that can satisfy a bounded current-cycle V30 access |

Avoid ambiguous architecture terms such as `internal Flash`, `on-board RAM`, `external RAM`, or `V30-visible RAM` when a more precise term exists.

## 3. Physical memory resources

```text
RP2350
|
+-- RP2350 Internal SRAM
|
+-- External NOR Flash
|
+-- External PSRAM
|
`-- SD Card (optional)
```

These resources have different responsibilities.

### RP2350 Internal SRAM

Primary uses:

- RP2350 firmware runtime state;
- PIO/DMA queues, descriptors, and staging;
- deterministic bus state;
- machine-control and fault state;
- explicitly prepared V30 code/data windows;
- short trace or fault-capture buffers.

Internal SRAM is not the default bulk V30 RAM resource.

### External NOR Flash

Primary uses:

- RP2350 firmware image;
- reserved recovery/fallback capacity;
- persistent configuration and metadata;
- filesystem-backed workloads and assets.

External NOR Flash is persistent storage, not an assumed current-cycle V30 memory responder.

### External PSRAM

External PSRAM is the target machine's bulk volatile backing/workspace.

Primary uses:

- bulk V30 writable backing state;
- workload staging;
- large buffers;
- snapshots;
- long trace storage;
- temporary service data.

External PSRAM is **not** automatically a deterministic current-cycle V30 responder. A current-cycle use requires a separately validated bounded response mechanism. Until then, data required by an active cycle must be explicitly prepared into deterministic on-chip state.

### SD Card

SD Card support is optional.

Its intended role is removable bulk storage for large assets, traces, snapshots, images, or offline exchange. SD latency must never become a synchronous dependency of a current V30 bus cycle.

## 4. Hardware configurations

Two configurations are distinguished deliberately.

### Bring-up configuration

```text
RP2350 Internal SRAM
+
External NOR Flash
```

This is sufficient for reset, small native workloads, diagnostics, deterministic bus validation, and SRAM-backed experiments.

### Target machine configuration

```text
RP2350 Internal SRAM
+
External NOR Flash
+
External PSRAM
```

External PSRAM is part of the target machine baseline because it separates bulk volatile machine state from scarce deterministic on-chip SRAM.

SD Card remains optional.

## 5. V30 Memory Map

The NEC V30 exposes a 20-bit physical address space:

```text
00000h ------------------------- FFFFFh
             1 MiB
```

The architecture does not impose the IBM PC conventional-memory/VGA/BIOS layout.

### Architectural minimum

At absolute minimum, the RP2350 must provide a deterministic instruction source for the V30 architectural reset fetch at `FFFF0h`.

### Minimum useful machine

A useful native machine configuration provides four semantics:

| Requirement | Meaning |
|---|---|
| **Reset Handoff Region** | deterministic instruction source covering the reset handoff sequence |
| **Executable Region** | memory from which the selected workload can execute |
| **Writable RAM Region** | stack, data, and mutable workload state |
| **Defined Unmapped Behavior** | safe, deterministic behavior for addresses not implemented by the selected machine configuration |

The exact locations and sizes of the executable and writable regions are workload/machine configuration choices.

Interrupt vectors, shared-memory windows, device memory, BIOS regions, VGA memory, and other compatibility mappings are optional capabilities rather than minimum requirements.

## 6. Memory-map semantics versus backing

A map entry describes what the V30 observes. Backing assignment describes how the RP2350 supplies that behavior.

Conceptually:

```text
V30 address
    |
    v
V30 Memory Map
    |
    +-- executable / read-only
    +-- writable RAM
    +-- service/device
    `-- unmapped
            |
            v
      Backing Resource
            |
            +-- RP2350 Internal SRAM
            +-- External PSRAM
            `-- prepared/generated state
```

A writable V30 region may have bulk state in External PSRAM while an explicitly prepared current working window is represented in RP2350 Internal SRAM. The map semantics do not change when the backing implementation changes.

## 7. Deterministic memory policy

The current Pi86 HAT keeps V30 `READY` asserted. Therefore a no-wait current-cycle response cannot depend on an unbounded M33 lookup, USB, filesystem operation, External NOR Flash access, or arbitrary External PSRAM latency.

The initial memory implementation uses **explicit prepared deterministic windows** rather than a general cache hierarchy.

```text
External PSRAM / persistent asset
              |
         prepare / stage
              v
RP2350 Internal SRAM
Deterministic V30 Window
              |
           PIO / DMA
              |
              v
        Physical V30
```

A general cache would introduce miss handling, replacement, dirty writeback, coherency, and refill deadlines. It is deferred until a demonstrated requirement justifies that complexity.

See [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

## 8. RP2350 Internal SRAM planning

The RP2350 provides 520 KB of internal SRAM. The following values are **planning budgets**, not fixed ABI partitions or linker addresses:

| Use | Initial planning budget |
|---|---:|
| Firmware runtime | ~128 KB |
| PIO/DMA/bus deterministic state | ~96 KB |
| Deterministic V30 Window | ~192 KB |
| Machine/mailbox state | ~32 KB |
| Trace and safety reserve | ~72 KB |
| **Total** | **520 KB** |

The exact partition must be derived from linker maps, PIO/DMA buffer requirements, USB usage, core stacks, and measured SRAM-bank contention.

Architecture rules:

- deterministic bus execution has priority over convenience buffers;
- long trace and snapshot history belongs in External PSRAM;
- realtime allocations are fixed before the machine enters `PREPARED` or `RUNNING`;
- the realtime path must not depend on heap allocation;
- SRAM-bank placement should minimize contention among DMA, PIO-facing buffers, and M33 instruction/data traffic.

## 9. Persistent filesystem

External NOR Flash may contain a filesystem for persistent machine assets.

The ownership rule is:

> **One filesystem, one owner, multiple clients.**

The RP2350 is the sole filesystem owner. Host operations are mediated through the Host Protocol. A V30 filesystem service is optional and, if implemented, is also mediated by the RP2350.

LittleFS is the baseline filesystem for External NOR Flash because the Flash is MCU-owned and does not need to be directly mounted by a host PC.

A minimal namespace may contain:

```text
/workloads/
/data/
/profiles/
/logs/
```

The namespace may be reduced or expanded only when required by actual workloads.

Exact Flash partition offsets are intentionally not fixed here. Firmware update/recovery requirements, linker layout, and filesystem sizing must be resolved before physical partition addresses become canonical.

## 10. Optional SD storage

If SD support is added, it remains an RP2350-owned optional storage backend. FAT32 is a reasonable implementation for removable host-readable media, but filesystem choice is not part of the core architecture.

Data required for deterministic execution must be staged from SD into appropriate working memory before it becomes an active V30 dependency.

## 11. Related documents

- [`architecture.md`](architecture.md) - overall system architecture
- [`host_protocol.md`](host_protocol.md) - Host/RP2350 command and observation contract
- [`adr/0007-adopt-host-constructed-v30-machine-model.md`](adr/0007-adopt-host-constructed-v30-machine-model.md) - machine-model decision
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - deterministic memory/READY policy
- [`pc1c0c1_arbitrary_sram_rom_architecture.md`](pc1c0c1_arbitrary_sram_rom_architecture.md) - historical address-qualified SRAM response research
