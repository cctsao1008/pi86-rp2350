# Pi86-RP2350 Hardware Contract

## Purpose

This document defines the non-negotiable hardware interface contract between the original Pi86/Homebrew8088 V20/V30 HAT and the Waveshare RP2350-PiZero host.

It exists to prevent a class of bring-up failures caused by mixing GPIO numbering domains. It is the canonical human-readable mapping reference for this repository. Firmware definitions live in `firmware/v30/v30_pins.h` and must remain consistent with this document.

## Known-good baseline

The original Pi86 V20/V30 HAT and the installed NEC V30 D70116C-8 are known to operate when the same HAT/CPU assembly is connected to a Raspberry Pi and driven by an older working Pi86 build/source baseline.

Therefore, when the RP2350 port exhibits a failure, the default debugging prior is:

1. target-board pin translation,
2. firmware GPIO ownership/direction,
3. clock/reset sequencing,
4. bus-service timing and transaction decoding,
5. only then physical HAT/CPU defects if new evidence requires that hypothesis.

The known-good Raspberry Pi configuration does not prove every electrical property of the RP2350 port, but it is strong evidence against casually reclassifying the HAT or CPU as defective.

## Canonical identity rule

The Raspberry Pi **physical 40-pin header position** is the cross-platform hardware ABI.

The translation chain is always:

```text
V30 signal
  -> Raspberry Pi physical header pin
  -> target-board pinout/schematic
  -> RP2350-PiZero GPIO
```

The following numbering domains are distinct and must never be conflated:

```text
WiringPi number != Raspberry Pi BCM GPIO != physical header pin != RP2350 GPIO
```

A Raspberry Pi BCM GPIO number is reference-platform metadata. It must never be copied directly into an RP2350 GPIO definition unless the RP2350-PiZero physical pinout independently confirms that mapping.

## Locked V30 HAT mapping

| V30 signal | RPi physical pin | RP2350-PiZero GPIO | Normal direction |
|---|---:|---:|---|
| CLK | 40 | 21 | RP2350 -> V30 |
| RESET | 36 | 16 | RP2350 -> V30 |
| ALE | 32 | 9 | V30 -> RP2350 |
| IO/M | 24 | 8 | V30 -> RP2350 |
| BUFR/W | 26 | 7 | V30 -> RP2350 |
| BHE | 22 | 25 | V30 -> RP2350 |
| INTR | 38 | 20 | RP2350 -> V30 |
| INTA | 28 | 1 | V30 -> RP2350 |
| AD0 | 37 | 26 | Bidirectional |
| AD1 | 35 | 19 | Bidirectional |
| AD2 | 33 | 13 | Bidirectional |
| AD3 | 31 | 6 | Bidirectional |
| AD4 | 29 | 15 | Bidirectional |
| AD5 | 27 | 0 | Bidirectional |
| AD6 | 23 | 10 | Bidirectional |
| AD7 | 21 | 12 | Bidirectional |
| AD8 | 19 | 11 | Bidirectional |
| AD9 | 15 | 22 | Bidirectional |
| AD10 | 13 | 27 | Bidirectional |
| AD11 | 11 | 17 | Bidirectional |
| AD12 | 7 | 14 | Bidirectional |
| AD13 | 5 | 3 | Bidirectional |
| AD14 | 3 | 2 | Bidirectional |
| AD15 | 8 | 4 | Bidirectional |
| A16 | 10 | 5 | V30 -> RP2350 |
| A17 | 12 | 18 | V30 -> RP2350 |
| A18 | 16 | 23 | V30 -> RP2350 |
| A19 | 18 | 24 | V30 -> RP2350 |

## Historical mapping failure and correction

An earlier RP2350 mapping incorrectly treated Raspberry Pi BCM GPIO numbers as though they were RP2350-PiZero GPIO numbers. The most visible example was:

```text
RPi physical pin 21 = BCM9
```

which was incorrectly translated as:

```text
RP2350 GPIO9
```

The actual RP2350-PiZero routing is:

```text
RPi physical pin 21 = RP2350 GPIO12
```

That error swapped the identities of important signals such as ALE and AD7 and also affected AD4, AD6, AD8, AD12, AD15 and A16. Several subsequent diagnostics were therefore accurately measuring the wrong physical signals.

Corrected mapping restored deterministic Gate 3 through Gate 6 behavior.

## Firmware rules

1. `firmware/v30/v30_pins.h` is the only firmware-level source of V30 GPIO constants.
2. V30 bus code must use `V30_PIN_*` definitions instead of undocumented raw GPIO literals.
3. PIO programs that must use an absolute GPIO must document the corresponding V30 signal and Raspberry Pi physical pin.
4. A mapping change requires simultaneous review of:
   - this document,
   - `docs/pin_mapping.md`,
   - `firmware/v30/v30_pins.h`,
   - PIO programs containing absolute GPIO waits or pin assignments.
5. Any new host board must recreate the translation from physical header position; BCM numbers are not a portable intermediate ABI.

## Signal-debugging rule

Before diagnosing the behavior of a signal, first prove its identity.

Required order:

```text
signal identity -> direction/ownership -> electrical state -> timing -> protocol meaning
```

Do not infer a physical fault from a GPIO observation until the physical-header-to-target-GPIO mapping is verified.

## Bus ownership rules

- AD0..AD15 are bidirectional and must be high-Z whenever the V30 owns the bus.
- RP2350 output values should be prepared before asserting AD output-enable.
- AD output-enable changes should remain atomic through direct SIO operations in timing-sensitive service paths.
- Safe halt state is `RESET=HIGH`, `CLK=LOW`, AD bus high-Z.
- Current validated execution model uses a software-stepped V30 clock compatible with early Pi86 sequencing.

## Current validated scope

As of the Gate 6 hardware run, the corrected mapping has been demonstrated with:

- Gate 3: reset release and stable first physical fetch at `0xFFFF0`.
- Gate 4: repeated aligned 16-bit memory reads with correct data-pad readback and prefetch-aware `EB FE` loop behavior.
- Gate 5: executable SRAM-backed ROM; far jump from reset vector to physical `0xF0000`; repeated target-loop execution.
- Gate 6: aligned 16-bit memory write to physical `0x00200`, backend storage of `0x1234`, CPU readback, compare and branch to the success path.

Not yet implied by this contract:

- odd-address word accesses,
- individual byte-lane support,
- general I/O transactions,
- interrupts,
- PSRAM timing,
- DOS/BIOS compatibility.

Those capabilities require separate validation.

## Source hierarchy for mapping decisions

Use this order when resolving a mapping discrepancy:

1. physical working-system evidence,
2. target-board official physical pinout / schematic,
3. HAT PCB routing,
4. original Pi86 software mapping,
5. WiringPi/BCM translation tables,
6. historical schematics/netlists,
7. inference.

Lower-priority sources must not silently override higher-priority physical evidence.
