# PC1-B PIO Direct Bus Response Frequency Sweep

## Summary

PC1-B validates a PIO/DMA-assisted direct bus response path on real NEC V30 hardware.

The objective is to remove M33 per-cycle polling and verify that RP2350 hardware-assisted timing can service the V30 bus deterministically.

## Architecture

```text
NEC V30
  |
  | bus cycle
  v
PIO0
  |
  | observation
  v
PIO1
  |
  | RX DREQ
  v
DMA
  |
  v
SIO GPIO response
```

## Validation Result

Instruction under test:

```text
EB FE  (JMP $)
```

Expected behavior:

```text
FFFF0 -> FFFF2 -> FFFF4 -> FFFF0
```

## Frequency Sweep

| V30 Clock | Result |
|---|---|
|0.300 MHz|PASS|
|0.600 MHz|PASS|
|1.200 MHz|PASS|
|2.000 MHz|PASS|
|3.000 MHz|PASS|
|4.000 MHz|PASS|
|5.000 MHz|PASS|
|6.000 MHz|PASS|
|7.000 MHz|PASS|
|8.000 MHz|PASS|

## Significance

The original Pi86 Raspberry Pi 2/3 implementation demonstrated approximately 0.3 MHz operation. This experiment validates that the RP2350 PIO/DMA architecture can sustain deterministic physical V30 bus servicing beyond that historical operating point.

This result does not yet represent full PC compatibility. The next milestone is execution of real ROM code with ROM, RAM, interrupt, and peripheral behavior enabled.

## Next Gate

PC1-C:

```text
PIO/DMA bus service
    -> ROM image execution
    -> BIOS-compatible initialization
    -> peripheral bring-up
```
