# Original Pi86 V30 HAT Pin Mapping

This mapping is a locked hardware ABI for the project. The HAT is not being redesigned or remapped.

The mapping below is corroborated by both the original Pi86 software and the current Homebrew8088 KiCad PCB source under `Kicad/Single Int/Single Chip.kicad_pcb`.

| V30 signal | Raspberry Pi physical pin | RP2350 GPIO / BCM | Direction during normal bus use |
|---|---:|---:|---|
| CLK | 40 | 21 | RP2350 -> V30 |
| RESET | 36 | 16 | RP2350 -> V30 |
| ALE | 32 | 12 | V30 -> RP2350 |
| IO/M | 24 | 8 | V30 -> RP2350 |
| DT/R | 26 | 7 | V30 -> RP2350 |
| BHE | 22 | 25 | V30 -> RP2350 |
| INTR | 38 | 20 | RP2350 -> V30 |
| INTA | 28 | 1 | V30 -> RP2350 |
| AD0 | 37 | 26 | Bidirectional |
| AD1 | 35 | 19 | Bidirectional |
| AD2 | 33 | 13 | Bidirectional |
| AD3 | 31 | 6 | Bidirectional |
| AD4 | 29 | 5 | Bidirectional |
| AD5 | 27 | 0 | Bidirectional |
| AD6 | 23 | 11 | Bidirectional |
| AD7 | 21 | 9 | Bidirectional |
| AD8 | 19 | 10 | Bidirectional |
| AD9 | 15 | 22 | Bidirectional |
| AD10 | 13 | 27 | Bidirectional |
| AD11 | 11 | 17 | Bidirectional |
| AD12 | 7 | 4 | Bidirectional |
| AD13 | 5 | 3 | Bidirectional |
| AD14 | 3 | 2 | Bidirectional |
| AD15 | 8 | 14 | Bidirectional |
| A16 | 10 | 15 | V30 -> RP2350 |
| A17 | 12 | 18 | V30 -> RP2350 |
| A18 | 16 | 23 | V30 -> RP2350 |
| A19 | 18 | 24 | V30 -> RP2350 |

## Fixed CPU straps / power in the current HAT PCB source

The current `Single Chip.kicad_pcb` also documents the following fixed connections:

| V30 pin/function | Connection |
|---|---|
| Pin 40 VCC | 3.3 V |
| Pin 22 READY | 3.3 V |
| Pin 33 MN/MX | 3.3 V |
| Pin 17 NMI | GND |
| Pin 23 TEST | GND |
| Pin 31 HOLD | GND |

The current board routes Raspberry Pi physical pin 4 (5 V) to the auxiliary FAN `5+` rail. Raspberry Pi physical pin 2 (5 V) is unconnected in the current PCB. The V30 VCC rail is therefore documented as 3.3 V, not 5 V.

## Source and revision caveat

The physical project HAT is a 2021 EMM board. The current Homebrew8088 GitHub KiCad source carries 2023 board/silkscreen data, including the 2023 `switched back to socket` revision.

Therefore:

- The current PCB source is authoritative for the current upstream design and strongly corroborates this mapping.
- The original Pi86 software independently corroborates the same GPIO assignments.
- The physical 2021 HAT must still be checked during Gate 2 for continuity, orientation, supply voltage, and any revision-specific differences.
- Legacy `Single Chip.sch` must not be used as the primary electrical source because it explicitly states `This does not match PCB`.

## Implementation consequence

AD0-AD15 are intentionally left in the original scattered mapping. Do not replace the HAT merely to obtain a contiguous GPIO bus. The firmware will optimize around this constraint using direct SIO snapshots, masks, and lookup-table-based output packing.

Bidirectional AD pins must be returned to a safe non-driving state whenever the V30 owns the data bus. Bus direction transitions are a hard real-time requirement and must not be implemented with slow per-pin GPIO APIs in the final bus service path.

## Safety

Do not run the GPIO sweep test with the V30 HAT installed. The GPIO test target configures GPIO0-GPIO27 as outputs specifically for temporary board validation.

The `Pi ALL GPIO TEST BOARD (A)` must also be removed before V30 timing tests. Its LED/resistor network is appropriate for Gate 1 continuity/mapping validation but adds avoidable loading to the V30 bus signals.
