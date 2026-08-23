# 8086 Core Validation

- Document type: validation specification, not one physical result
- Timing identity: defined by each implementing gate; this specification makes no clock-frequency claim
- Result identity: retained in the gate-specific validation record

## Purpose

Validate the essential architectural behavior of a physical 8086-compatible CPU connected to the RP2350 companion chipset.

The **Intel 8086 architecture is the common validation baseline**.

- NEC V30 runs the common 8086 tests plus separate NEC-specific tests.
- Intel/AMD 8086-class CPUs run the common tests.
- RP2350 observes CPU-visible results and externally visible bus behavior.

This document defines the stable core-validation scope. Test progress and measured results belong in gate, issue, or dedicated validation-result documents.

## Validation Principle

Each core test should provide two forms of evidence when applicable:

1. **CPU semantic evidence** — the test program verifies the architectural result, such as register values, FLAGS, control flow, or memory contents.
2. **RP2350 bus evidence** — the RP2350 observes the corresponding memory, I/O, or interrupt transaction when the behavior is externally visible.

Common tests use only the original 8086 instruction subset. NEC V30 extensions are tested separately.

```text
              Physical 8086-compatible CPU
                         |
            +------------+------------+
            |                         |
            v                         v
      CPU self-check            RP2350 observer
            |                         |
      semantic result            bus evidence
            |                         |
            +------------+------------+
                         |
                      PASS/FAIL
                         |
                      USB CDC
```

## Core Validation Set

| ID | Area | Core checks |
|---|---|---|
| C01 | Reset & Fetch | reset vector, instruction fetch, basic jump |
| C02 | ALU & FLAGS | add/subtract, logic, compare, FLAGS, conditional branch |
| C03 | Memory Access | byte/word read-write, even/odd addresses |
| C04 | Addressing | segment:offset and representative effective-address modes |
| C05 | I/O | IN/OUT, byte/word, even/odd ports |
| C06 | Stack & Control Flow | PUSH/POP, near/far CALL and RET |
| C07 | Interrupt | INTA, IVT, ISR entry, IRET |
| C08 | String Operations | MOVS/STOS/LODS/CMPS/SCAS and REP behavior |

The goal is not exhaustive instruction testing. The goal is to verify the architectural primitives that the RP2350 chipset, BIOS, and later software depend on.

## Result Observation

The CPU test ROM determines PASS or FAIL.

Port `0E9h` is the lightweight diagnostic channel already used by the project architecture:

```text
8086/V30 test ROM
       |
       | self-check
       |
       +---- OUT 0E9h, AL
                    |
                    v
             RP2350 observer
                    |
                    v
                USB CDC
                    |
                    v
                  Host
```

USB CDC is a presentation and logging path. It is not part of the cycle-critical CPU bus response path.

When a test fails, RP2350 bus capture is used to distinguish CPU-semantic failures from chipset/bus failures.

## CPU Compatibility

| Validation target | NEC V30 | Intel 8086 | AMD 8086 |
|---|:---:|:---:|:---:|
| 8086 core validation | Yes | Yes | Yes |
| NEC V30 extensions | Yes | No | No |
| Exact timing characterization | CPU-specific | CPU-specific | CPU-specific |

**Functional compatibility and exact physical timing are different validation targets.**

Instruction results, FLAGS, addressing, memory semantics, I/O semantics, stack behavior, and interrupt behavior belong to the common architectural baseline. Exact clocks, prefetch details, and implementation-specific timing are characterized separately.

## Scope Boundary

This document covers **CPU core validation only**.

The following are intentionally outside this scope:

- NEC V30-specific instructions and 8080 emulation;
- detailed prefetch and cycle-timing characterization;
- READY/wait-state experiments on hardware that cannot control READY;
- HOLD, bus arbitration, LOCK characterization, and DMA;
- peripheral implementation details such as 8255, 8259, and PIT;
- BIOS and DOS compatibility;
- RP2350 memory-backend implementation and performance.

These areas may use the core-validation results as prerequisites, but they should be tracked independently.

## Related Documents

- [`../architecture.md`](../architecture.md) — RP2350 companion-chip architecture and diagnostic path.
- [`../minimal_pc_compatibility_matrix.md`](../minimal_pc_compatibility_matrix.md) — current system-level validation and BIOS/DOS dependency progression.
