# PC1-C0C1-A Non-Driving SRAM Selector Feasibility Validation

- Date: 2026-08-20
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_arbitrary_rom_feasibility`
- Firmware commit: `a8291a5`
- UF2 size: 82,944 bytes
- UF2 SHA-256: `1A271EC9569E07C93FB35A97C7CDCA92FDA97FF26DF766D80F01DB7CA768CF73`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Measure whether PIO1 can select a ROM word from a DMA-fed internal-SRAM table
using the current physical early-T1 address before ASTB falls. The selected
entry is deliberately last at depths 1, 4, 8, 16, and 32.

This instrument never drives AD. It establishes a bounded selector result,
not an arbitrary-address ROM response service.

## Accepted result

All five fresh post-reset epochs selected `00EAh` at the correct final ordinal
while ASTB remained high. Depth 32 therefore establishes the measured lower
bound for this linear selector at 0.300 MHz. DMA and FIFO state drained to
zero, passive PIO0 observed `FFFF0`, every first-cycle phase witness completed,
and the terminal bus state was safe.

The result does **not** prove that the selected word can also be encoded,
transferred to the responder, and driven during the same no-wait bus cycle.
PC1-C0C1 remains open until that current-cycle response path is validated.

## Complete physical output

```text
PC1-C0C1-A Non-Driving SRAM Selector Feasibility - 0.300 MHz
Selector path       : current early-T1 raw key -> PIO1 scan
Table transport     : internal SRAM -> DMA -> PIO1 TX FIFO
Selected value      : PIO1 RX FIFO -> DMA -> internal SRAM
AD bus ownership    : passive SIO input for every stage
Input synchronizers : SDK defaults
Drive policy        : NONE; selected 00EA is never placed on AD

[SUMMARY]
depth address value ordinal before_ASTB_fall result
    1 PASS    PASS  PASS    PASS             PASS
    4 PASS    PASS  PASS    PASS             PASS
    8 PASS    PASS  PASS    PASS             PASS
   16 PASS    PASS  PASS    PASS             PASS
   32 PASS    PASS  PASS    PASS             PASS
Deepest contiguous scan = 32 entries
C0C1-A RESULT          = PASS

[ENGINEERING DETAILS]

-- scan depth 1 --
RESET / clean epoch      = PASS / PASS
PIO1 pre-release OE      = 00200000 PASS
Passive observer address = FFFF0 PASS
Selector capture raw     = 09C6DF3F PASS
Completion raw           = 09C6DF3F ASTB=1 CLK=0 PASS
Selected value / ordinal = 00EA / 1 PASS
DMA remain TX/RX/observer= 0/0/0
FIFO remain TX/RX/observer= 0/0/0
First-cycle phase words  = 6/6
Terminal safe/passive    = PASS / PASS
Stage result             = PASS

-- scan depth 4 --
RESET / clean epoch      = PASS / PASS
PIO1 pre-release OE      = 00200000 PASS
Passive observer address = FFFF0 PASS
Selector capture raw     = 09C6DF3F PASS
Completion raw           = 09C6DF3F ASTB=1 CLK=0 PASS
Selected value / ordinal = 00EA / 4 PASS
DMA remain TX/RX/observer= 0/0/0
FIFO remain TX/RX/observer= 0/0/0
First-cycle phase words  = 6/6
Terminal safe/passive    = PASS / PASS
Stage result             = PASS

-- scan depth 8 --
RESET / clean epoch      = PASS / PASS
PIO1 pre-release OE      = 00200000 PASS
Passive observer address = FFFF0 PASS
Selector capture raw     = 09C6DF3F PASS
Completion raw           = 09C6DF3F ASTB=1 CLK=0 PASS
Selected value / ordinal = 00EA / 8 PASS
DMA remain TX/RX/observer= 0/0/0
FIFO remain TX/RX/observer= 0/0/0
First-cycle phase words  = 6/6
Terminal safe/passive    = PASS / PASS
Stage result             = PASS

-- scan depth 16 --
RESET / clean epoch      = PASS / PASS
PIO1 pre-release OE      = 00200000 PASS
Passive observer address = FFFF0 PASS
Selector capture raw     = 09C6DF3F PASS
Completion raw           = 09C6DF3F ASTB=1 CLK=0 PASS
Selected value / ordinal = 00EA / 16 PASS
DMA remain TX/RX/observer= 0/0/0
FIFO remain TX/RX/observer= 0/0/0
First-cycle phase words  = 6/6
Terminal safe/passive    = PASS / PASS
Stage result             = PASS

-- scan depth 32 --
RESET / clean epoch      = PASS / PASS
PIO1 pre-release OE      = 00200000 PASS
Passive observer address = FFFF0 PASS
Selector capture raw     = 09C6DF3F PASS
Completion raw           = 09C6DF3F ASTB=1 CLK=0 PASS
Selected value / ordinal = 00EA / 32 PASS
DMA remain TX/RX/observer= 0/0/0
FIFO remain TX/RX/observer= 0/0/0
First-cycle phase words  = 6/6
Terminal safe/passive    = PASS / PASS
Stage result             = PASS

Interpretation: PASS proves bounded selector completion only.
No ROM response was driven; PC1-C0C1 remains open.
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Architecture conclusion

A current physical V30 early-T1 key can drive a deterministic PIO1 search of
at least 32 DMA-fed internal-SRAM descriptors before ASTB falls at 0.300 MHz.
The next C0C1 experiment must join selection to same-cycle AD/PINDIRS response
and retain explicit high-Z behavior for unsupported keys.
