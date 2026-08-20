# PC1-C0C1-A2 Same-Cycle Arbitrary Selector Response Validation

- Date: 2026-08-20
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_arbitrary_rom_response`
- Firmware commit: `6db4d2c`
- UF2 size: 87,040 bytes
- UF2 SHA-256: `277055F571D0582605C470FAA920322702C499BA56D1D6AAD835A006AB9D2E28`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Join the physically accepted current-address selector to same-cycle PIO1
AD/PINDIRS response. The selected `FFFF0h` descriptor is deliberately last at
depths 1, 4, 8, 16, and 32. A final depth-32 exact miss verifies that an
unsupported key produces no drive authorization.

The M33 is absent from the current-cycle path. PIO2 owns CLK, PIO1 owns
selection and scattered AD response, and PIO0 independently captures the
first address and six first-cycle phase snapshots.

## Accepted evidence

| Evidence | Physical result |
|---|---:|
| Hit depths | 1, 4, 8, 16, 32 PASS |
| Deepest selected ordinal | 32/32 PASS |
| Deadline gate | ASTB high at every hit |
| Driven response | R2/F2/R3 = `00EA/00EA/00EA` |
| Depth-32 exact miss | zero response words, PASS |
| Miss data-phase observation | `FFF0`, not authorized by PIO1 |
| PIO1 pre/post response OE | `00000000` / `00000000` |
| DMA/FIFO residue | zero except intentionally pending miss RX words |
| Terminal state | RESET high, CLK low, AD high-Z |

The hit-stage F1 value `00E0h` is the observed address-to-data turnaround
transition. The accepted V30 response window begins at R2, where the complete
word is stable and remains unchanged through F2 and R3.

This result proves a bounded 32-entry current-address selection plus first-word
same-cycle response. It does not yet prove multi-cycle table rewind/refill or
arbitrary program execution, so PC1-C0C1 remains open.

## Complete physical output

```text
PC1-C0C1-A2 Same-Cycle Arbitrary Selector Response - 0.300 MHz
Realtime path       : early-T1 key -> PIO1 scan -> AD/PINDIRS
Table transport     : internal SRAM -> DMA -> PIO1 TX FIFO
Current-cycle M33   : NONE
Clock owner         : PIO2; response owner: PIO1
Observer path       : passive PIO0 address + phase snapshots
Input synchronizers : SDK defaults
Late/miss policy    : no PINDIRS authorization; AD remains high-Z

[SUMMARY]
case depth address gate R2/F2/R3 no-drive result
HIT      1 PASS    PASS PASS     N/A      PASS
HIT      4 PASS    PASS PASS     N/A      PASS
HIT      8 PASS    PASS PASS     N/A      PASS
HIT     16 PASS    PASS PASS     N/A      PASS
HIT     32 PASS    PASS PASS     N/A      PASS
MISS    32 PASS    N/A  N/A      PASS     PASS
Deepest same-cycle response = 32 entries
Explicit depth-32 miss      = PASS
C0C1-A2 RESULT              = PASS

[ENGINEERING DETAILS]

-- hit depth 1 --
RESET / clean epoch       = PASS / PASS
PIO2 CLK OE / PIO1 pre-OE = 00200000 / 00000000 PASS
Passive observer address  = FFFF0 PASS
Response RX words          = 2/2
Selected ordinal           = 1/1 PASS
Deadline raw               = 09C6DF3F ASTB=1 PASS
R2/F2/R3 response          = 00EA/00EA/00EA PASS
DMA remain TX/RX/observer = 0/0/0
FIFO remain TX/RX/observer= 0/0/0
PIO1 post-release OE       = 00000000 PASS
First-cycle phase words    = 6/6
  AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
  R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
  F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
  R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA
Terminal safe              = PASS
Stage result               = PASS

-- hit depth 4 --
RESET / clean epoch       = PASS / PASS
PIO2 CLK OE / PIO1 pre-OE = 00200000 / 00000000 PASS
Passive observer address  = FFFF0 PASS
Response RX words          = 2/2
Selected ordinal           = 4/4 PASS
Deadline raw               = 09C6DF3F ASTB=1 PASS
R2/F2/R3 response          = 00EA/00EA/00EA PASS
DMA remain TX/RX/observer = 0/0/0
FIFO remain TX/RX/observer= 0/0/0
PIO1 post-release OE       = 00000000 PASS
First-cycle phase words    = 6/6
  AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
  R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
  F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
  R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA
Terminal safe              = PASS
Stage result               = PASS

-- hit depth 8 --
RESET / clean epoch       = PASS / PASS
PIO2 CLK OE / PIO1 pre-OE = 00200000 / 00000000 PASS
Passive observer address  = FFFF0 PASS
Response RX words          = 2/2
Selected ordinal           = 8/8 PASS
Deadline raw               = 09C6DF3F ASTB=1 PASS
R2/F2/R3 response          = 00EA/00EA/00EA PASS
DMA remain TX/RX/observer = 0/0/0
FIFO remain TX/RX/observer= 0/0/0
PIO1 post-release OE       = 00000000 PASS
First-cycle phase words    = 6/6
  AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
  R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
  F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
  R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA
Terminal safe              = PASS
Stage result               = PASS

-- hit depth 16 --
RESET / clean epoch       = PASS / PASS
PIO2 CLK OE / PIO1 pre-OE = 00200000 / 00000000 PASS
Passive observer address  = FFFF0 PASS
Response RX words          = 2/2
Selected ordinal           = 16/16 PASS
Deadline raw               = 09C6DF3F ASTB=1 PASS
R2/F2/R3 response          = 00EA/00EA/00EA PASS
DMA remain TX/RX/observer = 0/0/0
FIFO remain TX/RX/observer= 0/0/0
PIO1 post-release OE       = 00000000 PASS
First-cycle phase words    = 6/6
  AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
  R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
  F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
  R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA
Terminal safe              = PASS
Stage result               = PASS

-- hit depth 32 --
RESET / clean epoch       = PASS / PASS
PIO2 CLK OE / PIO1 pre-OE = 00200000 / 00000000 PASS
Passive observer address  = FFFF0 PASS
Response RX words          = 2/2
Selected ordinal           = 32/32 PASS
Deadline raw               = 09C6DF3F ASTB=1 PASS
R2/F2/R3 response          = 00EA/00EA/00EA PASS
DMA remain TX/RX/observer = 0/0/0
FIFO remain TX/RX/observer= 0/0/0
PIO1 post-release OE       = 00000000 PASS
First-cycle phase words    = 6/6
  AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
  R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
  F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
  R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA
Terminal safe              = PASS
Stage result               = PASS

-- miss depth 32 --
RESET / clean epoch       = PASS / PASS
PIO2 CLK OE / PIO1 pre-OE = 00200000 / 00000000 PASS
Passive observer address  = FFFF0 PASS
Response RX words          = 0/0
R2/F2/R3 response          = FFF0/FFF0/FFF0 NOT AUTHORIZED
DMA remain TX/RX/observer = 0/2/0
FIFO remain TX/RX/observer= 0/0/0
PIO1 post-release OE       = 00000000 PASS
First-cycle phase words    = 6/6
  AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
  F1 raw=09C6DD3F ASTB=0 CLK=0 AD=FFF0
  R2 raw=0866DD1F ASTB=0 CLK=1 AD=FFF0
  F2 raw=0846DD1F ASTB=0 CLK=0 AD=FFF0
  R3 raw=0866DD1F ASTB=0 CLK=1 AD=FFF0
Terminal safe              = PASS
Stage result               = PASS

Interpretation: PASS proves bounded current-address selection and
same-cycle first-word response only; general C0C1 ROM remains open.
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Architecture conclusion

At 0.300 MHz, PIO1 can scan at least 32 DMA-fed internal-SRAM descriptors,
select the current physical `FFFF0h` key, and place the correct word on the
scattered AD bus before the V30's accepted R2 sampling window. An exact miss
does not authorize AD ownership. The next stage is a bounded multi-cycle ROM
window whose lookup state resets correctly for every physical bus cycle.
