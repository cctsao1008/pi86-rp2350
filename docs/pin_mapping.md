# Original Pi86 V30 HAT Pin Mapping

This mapping is a locked hardware ABI for the project. The HAT is not being redesigned or remapped.

## Canonical mapping rule

The physical Raspberry Pi 40-pin header position is the canonical reference:

`V30 signal -> Raspberry Pi physical pin -> Raspberry Pi BCM GPIO -> RP2350-PiZero GPIO`

The original Pi86 software uses WiringPi numbering and therefore ultimately refers to Raspberry Pi BCM GPIOs. The Waveshare RP2350-PiZero preserves the Raspberry Pi-compatible **physical header layout**, but its RP2350 GPIO number at a given physical position is not always the same number as the Raspberry Pi BCM GPIO at that position.

Therefore **BCM GPIO numbers must never be copied directly into RP2350 firmware definitions without translating through the physical header pin**.

## Authoritative signal mapping

| V30 signal | RPi physical pin | Raspberry Pi BCM GPIO | RP2350-PiZero GPIO | Direction during normal bus use |
|---|---:|---:|---:|---|
| CLK | 40 | 21 | 21 | RP2350 -> V30 |
| RESET | 36 | 16 | 16 | RP2350 -> V30 |
| ALE / ASTB | 32 | 12 | 9 | V30 -> RP2350 |
| IO/M | 24 | 8 | 8 | V30 -> RP2350 |
| DT/R | 26 | 7 | 7 | V30 -> RP2350 |
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

The RP2350-PiZero physical-header mapping above is based on the Waveshare PiZero pinout used for this project. The original Pi86 HAT side remains defined by the Raspberry Pi physical header positions.

## Mapping correction made during bring-up

An earlier firmware revision incorrectly treated Raspberry Pi BCM GPIO numbers as though they were RP2350-PiZero GPIO numbers. That happened to be correct for several physical positions, but not all of them.

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

Any Gate 3 or Gate 4 result produced before this correction that depended on ALE, decoded address bits, or AD data values must be treated as provisional and revalidated with the corrected mapping.

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

The upstream design routes Raspberry Pi physical pin 4 (5 V) to the auxiliary FAN `5+` rail. Raspberry Pi physical pin 2 (5 V) is unconnected in the referenced upstream PCB source. The CPU supply arrangement is therefore documented separately from the host GPIO-number translation above.

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

## Safety

Do not run the GPIO sweep test with the V30 HAT installed. The GPIO test target configures GPIO0-GPIO27 as outputs specifically for temporary board validation.

The `Pi ALL GPIO TEST BOARD (A)` must also be removed before V30 timing tests. Its LED/resistor network is appropriate for Gate 1 continuity/mapping validation but adds avoidable loading to the V30 bus signals.
