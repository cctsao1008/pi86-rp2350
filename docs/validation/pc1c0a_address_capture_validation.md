# PC1-C0A Address Capture Validation

- Date: 2026-08-17
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_address_capture`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Validate passive, paired address/control capture under a continuous V30 clock before PC1-C begins driving address-qualified ROM responses.

The measurement epoch preserves the PC1-B reset discipline:

```text
clock-only RESET qualification
        |
        v
stop CLK LOW
        |
        v
arm passive PIO0 capture state machines
        |
        v
release RESET and restart continuous clock
```

PIO1 and DMA are unused. All sixteen AD pins remain assigned to SIO inputs with output enable clear.

## Acceptance result

```text
RESET clock count          = PASS
PRE-RESET EVENT LEAK       = NO
FIRST post-reset address   = FFFF0 PASS
FIRST cycle type           = MEMORY READ PASS
AD bus ownership           = PASSIVE PASS
MEASUREMENT EPOCH          = VALID
PC1-C ADDRESS CAPTURE RESULT = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Address/control evidence

The first four captured transactions were coherent sequential memory reads:

| Index | Address | IO/M | direction discriminator | INTA | BHE | A0 |
|---:|---:|---:|---:|---:|---:|---:|
| 00 | `FFFF0` | 1 | 0 | 1 | 0 | 0 |
| 01 | `FFFF2` | 1 | 0 | 1 | 0 | 0 |
| 02 | `FFFF4` | 1 | 0 | 1 | 0 | 0 |
| 03 | `FFFF6` | 1 | 0 | 1 | 0 | 0 |

For each pair, the address snapshot had ALE high and the following control snapshot had ALE low. The decoded address remained stable across the transition.

The next two transactions provided incidental byte-lane evidence:

| Index | Address | direction discriminator | BHE | A0 | Interpretation |
|---:|---:|---:|---:|---:|---|
| 04 | `0FFFD` | 1 | 0 | 1 | odd-address high-lane portion |
| 05 | `0FFFE` | 1 | 1 | 0 | following even-address low-lane portion |

This is consistent with a split odd-address word write. It is supporting observer evidence, not a PC1-C RAM-write acceptance test.

## Interpretation boundary

PC1-C0A deliberately supplies no read data. After the initial reset-vector reads, the V30 consumes uncontrolled bus values and its later transactions are not architecturally meaningful. They do not invalidate the capture test and must not be treated as ROM-execution evidence.

The captured direction signal is electrically coherent: it is low for the initial reads and high for the later writes. The historical firmware name `DTR` remains provisional until continuity testing identifies whether Raspberry Pi physical pin 26 reaches V30 `DT/R`, `RD#`, or `WR#`.

## Consequence

The passive continuous-clock capture front end is accepted for PC1-C. The next active boundary is **PC1-C0B Qualified Reset-Vector Response**, beginning at 0.300 MHz and returning `EA 00 00 00 F0` only for qualified reads of `FFFF0..FFFF4`.
