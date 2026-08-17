# Original Pi86 V30 HAT Pin Mapping

This mapping is a locked hardware ABI for the project. The HAT is not being redesigned or remapped.

The normative project contract is [`hardware_contract.md`](hardware_contract.md). This document is the implementation-oriented mapping table.

## Canonical mapping rule

The physical Raspberry Pi 40-pin header position is the canonical cross-platform reference:

```text
V30 signal
  -> Raspberry Pi physical pin
  -> target-board physical routing
  -> RP2350-PiZero GPIO
```

The following are separate namespaces:

```text
WiringPi number != Raspberry Pi BCM GPIO != physical header pin != RP2350 GPIO
```

The original Pi86 software uses WiringPi numbering and can be translated to Raspberry Pi BCM GPIOs when interpreting the reference implementation. That translation is **reference-platform metadata only**. The RP2350 GPIO must be obtained independently from the RP2350-PiZero physical pinout/schematic for the same physical header position.

Therefore **BCM GPIO numbers must never be copied directly into RP2350 firmware definitions**.

## Authoritative signal mapping

| V30 signal | RPi physical pin | Raspberry Pi BCM GPIO (reference metadata) | RP2350-PiZero GPIO | Direction during normal bus use |
|---|---:|---:|---:|---|
| CLK | 40 | 21 | 21 | RP2350 -> V30 |
| RESET | 36 | 16 | 16 | RP2350 -> V30 |
| ALE / ASTB | 32 | 12 | 9 | V30 -> RP2350 |
| IO/M | 24 | 8 | 8 | V30 -> RP2350 |
| BUFR/W | 26 | 7 | 7 | V30 -> RP2350 |
| BHE | 22 | 25 | 25 | V30 -> RP2350 |
| INTR | 38 | 20 | 20 | RP2350 -> V30 |
| INTA | 28 | 1 | 1 | V30 -> RP2350 |
| AD0 | 37 | 26 | 26 | Bidirectional |
| AD1 | 35 | 19 | 19 | Bidirectional |
| AD2 | 33 | 13 | 13 | Bidirectional |
| AD3 | 31 | 6 | 6 | Bidirectional |
| AD4 | 29 | 5 | 15 | Bidirectional |
| AD5 | 27 | 0 | 0 | Bidirectional |
| AD6 | 23 | 11 | 10 | Bidirectional |
| AD7 | 21 | 9 | 12 | Bidirectional |
| AD8 | 19 | 10 | 11 | Bidirectional |
| AD9 | 15 | 22 | 22 | Bidirectional |
| AD10 | 13 | 27 | 27 | Bidirectional |
| AD11 | 11 | 17 | 17 | Bidirectional |
| AD12 | 7 | 4 | 14 | Bidirectional |
| AD13 | 5 | 3 | 3 | Bidirectional |
| AD14 | 3 | 2 | 2 | Bidirectional |
| AD15 | 8 | 14 | 4 | Bidirectional |
| A16 | 10 | 15 | 5 | V30 -> RP2350 |
| A17 | 12 | 18 | 18 | V30 -> RP2350 |
| A18 | 16 | 23 | 23 | V30 -> RP2350 |
| A19 | 18 | 24 | 24 | V30 -> RP2350 |

The RP2350-PiZero GPIO mapping above is based on the Waveshare PiZero physical pinout used for this project. The original Pi86 HAT side remains defined by the Raspberry Pi physical header positions.

## Mapping correction made during bring-up

An earlier firmware revision incorrectly treated Raspberry Pi BCM GPIO numbers as though they were RP2350-PiZero GPIO numbers. That happened to be numerically identical for several physical positions, but not all of them.

The affected signals were:

| Signal | Incorrect RP2350 GPIO | Correct RP2350 GPIO |
|---|---:|---:|
| ALE / ASTB | 12 | 9 |
| AD4 | 5 | 15 |
| AD6 | 11 | 10 |
| AD7 | 9 | 12 |
| AD8 | 10 | 11 |
| AD12 | 4 | 14 |
| AD15 | 14 | 4 |
| A16 | 15 | 5 |

Gate results generated before this correction that depended on these signal identities are superseded unless they were explicitly revalidated after the correction.

The corrected mapping has since been demonstrated through:

- Gate 3 stable first fetch at physical `0xFFFF0`
- Gate 4 aligned memory-read execution with correct data pad readback
- Gate 5 executable SRAM-backed ROM and far-jump behavior
- Gate 6 aligned RAM write/readback plus CPU compare/branch success

## Fixed CPU straps / power in the upstream HAT design

The upstream Pi86 KiCad sources document the following fixed connections:

| V30 pin/function | Connection |
|---|---|
| Pin 40 VCC | 3.3 V |
| Pin 22 READY | 3.3 V |
| Pin 33 mode strap | 3.3 V |
| Pin 17 NMI | GND |
| Pin 23 TEST/POLL-related input | GND |
| Pin 31 HOLD | GND |

The upstream design routes Raspberry Pi physical pin 4 (5 V) to the auxiliary FAN `5+` rail. Raspberry Pi physical pin 2 (5 V) is unconnected in the referenced upstream PCB source. The CPU supply arrangement is documented separately from the host GPIO-number translation above.

## Source and revision caveat

The physical project HAT is an earlier board than the current upstream Homebrew8088 PCB revision. Upstream KiCad files are useful for confirming the HAT's **physical header routing**, while the RP2350-PiZero GPIO translation must come from the actual Waveshare board pinout.

Do not use any of these as interchangeable numbering systems:

- Raspberry Pi physical pin number
- WiringPi number
- Raspberry Pi BCM GPIO number
- RP2350-PiZero GPIO number

The physical header pin is the bridge between the Pi86 HAT and the RP2350-PiZero host.

## Implementation consequence

AD0-AD15 remain intentionally scattered in RP2350 GPIO space. Firmware must optimize around this fixed hardware interface using direct SIO snapshots, masks, and lookup-table-based output packing.

Bidirectional AD pins must be returned to a safe non-driving state whenever the V30 owns the data bus. Bus direction transitions are a hard real-time requirement and must not be implemented with slow per-pin GPIO APIs in the final bus service path.

## Review rule

A pin-map change is incomplete until all of the following agree:

- `docs/hardware_contract.md`
- this file
- `firmware/v30/v30_pins.h`
- any PIO source containing absolute GPIO references

## Safety

Do not run the GPIO sweep test with the V30 HAT installed. The GPIO test target configures GPIO0-GPIO27 as outputs specifically for temporary board validation.

The `Pi ALL GPIO TEST BOARD (A)` must also be removed before V30 timing tests. Its LED/resistor network is appropriate for Gate 1 continuity/mapping validation but adds avoidable loading to the V30 bus signals.
