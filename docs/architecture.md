# Architecture

## Goal

Replace the original Pi86 Raspberry Pi/Linux/WiringPi bus-control path with deterministic RP2350 firmware while keeping the physical NEC V30 and the original Pi86 V20/V30 HAT.

## Locked constraint

The V30 HAT is used as-is and plugs directly into the RP2350-PiZero. The original Pi86 signal mapping is preserved; no HAT PCB redesign is planned.

## Target partitioning

```text
NEC V30
   |
Original Pi86 V30 HAT
   |
GPIO0..GPIO27 (scattered original mapping)
   |
RP2350-PiZero
   |
   +-- PIO: clock generation / cycle timing synchronization
   +-- Core 0 + SIO: bus-critical service path
   |      +-- 32-bit GPIO snapshots
   |      +-- AD bus pack/unpack
   |      +-- memory and I/O transactions
   |      +-- interrupt acknowledge
   |
   +-- Core 1: slower services
          +-- MicroSD / disk images
          +-- CGA rendering / DVI
          +-- USB debug / keyboard
```

## Performance principle

The original Pi86 HAT scatters V30 AD0-AD15 across the Raspberry Pi GPIO namespace. The port therefore does not depend on contiguous PIO `IN PINS,16` / `OUT PINS,16` operations. Instead, the intended fast path is:

1. Take a 32-bit SIO GPIO snapshot.
2. Decode the scattered V30 signals using masks/shifts.
3. For data output, use precomputed lookup tables or equivalent bit packing.
4. Use PIO where deterministic clock/timing sequencing provides value.

## Memory progression

- Early bring-up: internal RP2350 SRAM only.
- Full Pi86 system: external PSRAM for the V30 physical address space and host-private buffers.
- Slow devices such as MicroSD must not be placed directly in the hard real-time bus path.

## Source of truth

NEC V20/V30 original documentation is normative for electrical and bus timing. Original Pi86 source is the compatibility reference for system behavior and HAT mapping.
