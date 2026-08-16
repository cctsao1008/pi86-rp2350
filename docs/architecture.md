# Architecture

## Goal

Replace the original Pi86 Raspberry Pi/Linux/WiringPi bus-control path with deterministic RP2350 firmware while keeping the physical NEC V30 and the original Pi86 V20/V30 HAT.

## Locked hardware constraint

The V30 HAT is used as-is and plugs directly into the RP2350-PiZero. No HAT PCB redesign or signal reassignment is planned.

The Raspberry Pi **physical 40-pin header position** is the cross-platform hardware ABI. Host GPIO numbers are target-specific and must be translated through the physical header position. See `docs/hardware_contract.md`.

## Validated execution baseline

The corrected host mapping has passed:

- Gate 3: RESET -> first fetch at physical `0xFFFF0`.
- Gate 4: aligned 16-bit memory read and prefetch-aware execution.
- Gate 5: internal-SRAM-backed executable ROM and far jump to physical `0xF0000`.
- Gate 6: aligned 16-bit RAM write/readback with CPU compare/branch verification.

The current refactor must preserve this behavior before new byte-lane capability is accepted.

## Runtime partitioning

```text
NEC V30
   |
Original Pi86 V30 HAT
   |
Raspberry Pi-compatible physical 40-pin header
   |
RP2350-PiZero target GPIO mapping
   |
   +-- PIO: software-stepped V30 clock primitive
   |
   +-- Core 0 + SIO: bus-critical service path
   |      +-- GPIO snapshot / signal decode
   |      +-- AD bus pack/unpack and ownership
   |      +-- bus-cycle classification
   |      +-- memory / I/O dispatch
   |      +-- interrupt acknowledge
   |
   +-- Core 1: slower services
          +-- MicroSD / disk images
          +-- CGA rendering / DVI
          +-- USB debug / keyboard
```

## Software layering

The bring-up implementation is being separated into replaceable layers:

```text
V30 physical bus
      |
      v
firmware/v30/v30_bus.*
      - stepped CLK()/RESET sequencing
      - ALE/T1 address capture
      - A0/BHE# lane decode
      - memory/I/O cycle classification
      - AD direction/ownership
      - read response / write capture phases
      |
      v
transaction dispatch / system policy
      |
      +-------------------+
      |                   |
      v                   v
memory backend          I/O backend (future)
firmware/memory/*       devices/* (future)
      |
      +-- internal SRAM during bring-up
      +-- external PSRAM later
```

The bus layer must not own the memory map. The memory layer must not own V30 timing or GPIO semantics.

## Bus API design rules

- Signal names preserve polarity where ambiguity matters (`BHE#`, `INTA#`).
- A0 and BHE# are decoded into explicit active byte lanes.
- AD0..AD15 remain high-Z whenever the V30 owns the data bus.
- Output values are prepared before the relevant AD output-enable mask is asserted.
- Read service may enable low byte, high byte, or both lanes independently.
- Write capture preserves the data phase already demonstrated by Gate 6 unless new measured evidence requires a change.
- I/O and interrupt policy remain outside the memory backend.

## Regression-before-extension rule

A structural refactor and a new bus capability must not be validated in the same first test.

Current sequence:

```text
Gate 6 PASS
   |
   v
extract v30_bus + memory backend
   |
   v
Gate 6R architecture regression
   |  must reproduce 0x00200 <- 0x1234 -> CPU readback -> SUCCESS
   v
Gate 7 byte-lane / odd-address validation
```

If Gate 6R fails, Gate 7 is not executed until the regression is resolved.

## Performance principle

The original Pi86 HAT scatters V30 AD0-AD15 across RP2350 GPIO space. The fast path therefore does not depend on contiguous PIO `IN PINS,16` / `OUT PINS,16` operations. Instead:

1. take a 32-bit SIO GPIO snapshot;
2. decode scattered V30 signals with masks/shifts or equivalent precomputation;
3. use lookup tables for data output packing;
4. use lane-specific OE masks for byte transactions;
5. use PIO where deterministic clock/timing sequencing provides value.

Correctness and complete transaction semantics are established before clock-rate optimization.

## Memory progression

- Bring-up: internal RP2350 SRAM-backed RAM/ROM.
- Current memory API is byte-addressed so aligned words, byte lanes and odd-address operations can share one backend.
- Full Pi86 system: external PSRAM for V30 system memory and host-private buffers.
- Slow devices such as MicroSD must not be placed directly in the hard real-time bus path.

## Source of truth

- `docs/hardware_contract.md` is the project-owned hardware interface contract.
- NEC V20/V30 documentation is normative for CPU electrical/bus semantics.
- Original Pi86 behavior is the compatibility reference for software-stepped sequencing and system behavior.
- Actual corrected hardware evidence outranks historical diagnostics produced under the superseded GPIO mapping.
