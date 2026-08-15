# Original Pi86 V30 HAT Pin Mapping

This mapping is a locked hardware ABI for the project. The HAT is not being redesigned.

| V30 signal | RP2350 GPIO / BCM |
|---|---:|
| CLK | 21 |
| RESET | 16 |
| ALE | 12 |
| IO/M | 8 |
| DT/R | 7 |
| BHE | 25 |
| INTR | 20 |
| INTA | 1 |
| AD0 | 26 |
| AD1 | 19 |
| AD2 | 13 |
| AD3 | 6 |
| AD4 | 5 |
| AD5 | 0 |
| AD6 | 11 |
| AD7 | 9 |
| AD8 | 10 |
| AD9 | 22 |
| AD10 | 27 |
| AD11 | 17 |
| AD12 | 4 |
| AD13 | 3 |
| AD14 | 2 |
| AD15 | 14 |
| A16 | 15 |
| A17 | 18 |
| A18 | 23 |
| A19 | 24 |

## Implementation consequence

AD0-AD15 are intentionally left in the original scattered mapping. Do not replace the HAT merely to obtain a contiguous GPIO bus. The firmware will optimize around this constraint using direct SIO snapshots, masks, and lookup-table-based output packing.

## Safety

Do not run the GPIO sweep test with the V30 HAT installed. The GPIO test target configures GPIO0-GPIO27 as outputs specifically for temporary board validation.
