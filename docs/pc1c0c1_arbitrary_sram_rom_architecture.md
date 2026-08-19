# PC1-C0C1 Arbitrary-Address Internal-SRAM ROM Architecture

## Objective

PC1-C0C1 must return ROM data selected from the current physical V30 address,
not from a prestaged execution-path index.

The milestone is complete only when taken branches, repeated fetches, and
non-linear control flow work inside a declared ROM range without knowing the
next transaction in advance.

## Accepted baseline

The descriptor-fed C0C0 engine is already physically accepted and remains a
permanent regression:

- PIO1 owns synchronized CLK, exact early-T1 qualification, and AD/PINDIRS;
- DMA feeds bounded key and response streams from internal SRAM;
- PIO0 and DMA retain independent address/R2-data evidence;
- the M33 is absent from the current-cycle response path;
- misses stay high-Z and observable;
- terminal RESET-high, CLK-low, AD-high-Z is mandatory.

`PC1-C0C0-H` additionally proved real Native BIOS writes to diagnostic port
`0xE9`. Neither result is arbitrary-address service because ROM responses still
follow a known descriptor order.

## Hardware constraint

The golden HAT ties V30 `READY` high. It cannot extend an individual bus cycle
while a cache, PSRAM backend, M33 service, or peripheral prepares data.

Therefore every C0C1 response on the current HAT must be one of:

1. a deterministic on-chip hit that meets the fixed no-wait deadline; or
2. an explicitly unsupported miss that leaves AD high-Z and is recorded.

Clock stopping is not the miss policy for the installed standard D70116C-8.
The V3.0 HAT must expose a PIO-controlled `READY` path for bounded dynamic
ROM/RAM/peripheral wait states. ADR 0003 is normative.

## Required data path

```text
physical ASTB/T1 address + control
        -> qualify MEMR and byte lanes
        -> select data by current A19:A0
        -> bounded internal-SRAM/on-chip hit structure
        -> encoded GPIO0-27 response descriptor
        -> PIO1 AD/PINDIRS at the validated data phase

independent PIO0 observer
        -> DMA -> SRAM address/R2-data evidence
```

The phrase "internal-SRAM ROM" describes the authoritative ROM image and
real-time tables. It does not permit an M33 current-cycle lookup. Any transfer
from SRAM to the PIO responder must have a measured, fixed worst-case path.

## Design questions to resolve before implementation

- What is the smallest useful arbitrary-address ROM window?
- Which PIO/DMA/on-chip selection structure can use the current address within
  the no-wait deadline?
- Is the guaranteed-hit structure fully resident, banked, or cached?
- How are A0/UBE byte lanes and an aligned word crossing handled?
- How does a miss inhibit AD drive without consuming an unrelated response?
- Can DMA arbitration, FIFO state, or refill ever substitute stale data?
- What address-set guarantee can be stated for the current HAT?

Candidate structures must be rejected if correctness depends on average
latency, predicted control flow, or repairing a wrong word after the V30 has
consumed it.

## Prototype stages

### C0C1-A: deadline and selector feasibility

- preserve the accepted early-T1 sample point and default synchronizers;
- measure the capture-to-drive instruction/cycle budget;
- prototype at least one selector driven by the current raw address;
- prove deterministic hit and explicit high-Z miss behavior;
- do not claim a ROM range until every address in it has the same guarantee.

#### Implemented non-driving feasibility instrument

`pc1c_arbitrary_rom_feasibility` is the first C0C1-A measurement target. One
UF2 runs fresh qualified RESET epochs with selector-table depths 1, 4, 8, 16,
and 32. Each internal-SRAM table entry is a three-word tuple containing the
raw early-T1 key, candidate ROM word, and ordinal. The target `FFFF0` entry is
deliberately last, so each stage measures the worst case for that depth.

PIO1 captures the physical raw key and scans the DMA-fed table without an M33
round trip. It returns the selected word, ordinal, and a completion GPIO
snapshot through its RX FIFO to DMA. A stage passes only when selection is
correct and the completion snapshot still has ASTB high, before the validated
response deadline. An independent PIO0/DMA witness, first-cycle phase capture,
passive AD ownership, and terminal safe-state checks remain enabled.

This probe contains no AD/PINDIRS drive operation. A physical PASS establishes
only a bounded non-driving selector depth; it does not complete C0C1 or prove
that the selected word can yet be driven within the same cycle.

The physical 2026-08-20 baseline passed at every tested depth through 32. At
depth 32, PIO1 selected `00EAh` at ordinal 32 while ASTB was still high; DMA,
FIFO, phase-witness, passive-ownership, and terminal-safety gates also passed.
See
[`validation/pc1c0c1a_selector_feasibility_validation.md`](validation/pc1c0c1a_selector_feasibility_validation.md).

```bash
./scripts/build.sh --target pc1c_arbitrary_rom_feasibility
```

The UF2 is generated at
`build/tests/performance_characterization_1_extended/pc1c_arbitrary_rom_feasibility.uf2`.

### C0C1-B: bounded arbitrary-address ROM window

- declare base, size, alignment, and lane rules;
- fill the window from a generated NASM ROM image in internal SRAM;
- execute forward, backward, and repeated fetches in an order not represented
  by a descriptor stream;
- include taken conditional branches and a non-linear jump target;
- retain exact passive address and R2-data evidence.

### C0C1-C: generalization and READY boundary

- determine the largest provable current-HAT hit set;
- define refill behavior strictly outside a CPU-visible miss cycle;
- carry the same service contract to V3.0;
- use controllable READY for bounded misses, PSRAM, RAM, and slow peripherals.

## Acceptance criteria

- response data is selected by the current physical address and cycle type;
- correctness is independent of expected transaction order;
- arbitrary in-range addresses, taken branches, and repeated fetches pass;
- byte-lane and alignment behavior is explicit and physically validated;
- unsupported addresses remain high-Z;
- no miss can return a stale, speculative, or unrelated word;
- worst-case hit latency is bounded and measured;
- deadline misses and unqualified drives are zero;
- passive PIO0/DMA evidence and terminal safe state are retained;
- the original C0C0 and C0C0-H regression targets continue to pass unchanged.

## Non-goals for the first baseline

- direct PSRAM response;
- DOS or a full compatibility BIOS;
- frequency sweep before the 0.300 MHz functional baseline passes;
- using READY on the unmodified golden HAT;
- replacing the descriptor-fed regression engine.

Tracking: GitHub issues #46, #29, and #48.
