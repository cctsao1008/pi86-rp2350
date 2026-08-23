# PC1-B PIO Direct Bus Response Frequency Sweep

- Firmware target: `pc1b_pio_direct_post_reset_epoch_sweep`
- Timing identity: configured continuous-clock sweep from 0.300 through 8.000 MHz

## Summary

PC1-B validates a PIO/DMA-assisted direct bus response path on real NEC V30 hardware.

The objective is to remove M33 per-cycle polling and verify that RP2350 hardware-assisted timing can service the V30 bus deterministically.

## Architecture

```text
NEC V30
  |
  | bus cycle
  v
PIO1 direct bus-phase response
  ^
  | TX DREQ
DMA <- encoded GPIO0-27 words in SRAM
  |
  +-> PIO1 TX FIFO -> OUT pins, 28 -> MOV PINDIRS

PIO0 remains a passive clock/ALE/phase observer.
```

DMA does not write SIO in this validated path. Only the scattered AD pins are function-muxed to PIO1; intervening GPIOs in the contiguous OUT window remain owned by their configured functions.

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

The original Pi86 Raspberry Pi 2/3 implementation demonstrated approximately 0.3 MHz operation. This experiment validates the fixed, pre-staged PIO-direct response path beyond that historical operating point, through every configured clock tested up to 8.000 MHz.

This result does not yet establish arbitrary 8 MHz address-dependent memory service or full PC compatibility. The next milestone is address-qualified ROM execution, followed by RAM and integrated peripheral behavior.

## Next Gate

PC1-C:

```text
PIO/DMA bus service
    -> ROM image execution
    -> BIOS-compatible initialization
    -> peripheral bring-up
```
