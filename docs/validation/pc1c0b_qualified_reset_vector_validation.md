# PC1-C0B Qualified Reset-Vector Validation

- Date: 2026-08-17
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_qualified_reset_vector`
- Firmware commit: `8e6aa1b`
- Firmware SHA-256: `5385AA0360E4F2C3A09AC5E89A228B6BD1025BA38CAC9EA7D65327D74C83D961`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Prove that the RP2350 can qualify current physical V30 memory-read addresses,
return the reset-vector far jump through scattered AD pins, and cause the V30
to fetch the target at `F0000` without an M33 current-cycle round trip.

The PIO1 matcher and responder FIFOs were prestaged while RESET was asserted
and CLK was stopped low. PIO1 then performed exact early-T1 raw-key matching,
signalled the responder through an internal PIO IRQ, and controlled AD/PINDIRS.
PIO0 and DMA independently recorded the passive address trace.

## Acceptance result

```text
RESET clock count         = PASS
PRE-RESET EVENT LEAK      = NO
PIO1 pre-release OE       = 00200000 CLK-ONLY PASS
FIRST post-reset address  = FFFF0 PASS
FIRST cycle type          = MEMORY READ PASS
Matcher FIFO primed       = 4/4 PASS
Responder FIFO primed     = 4/4 PASS
DMA observer first address= FFFF0 PASS
DMA observer FIFO residue = 0 PASS
FIRST response at R2/F2/R3= 00EA PASS
Required ROM hit mask     = 07 PASS
Response deadline misses  = 0 PASS
PIO-qualified pairs       = 4/4 PASS
Responder FIFO remain     = 0 PASS
Far-jump target observed  = F0000 PASS
MEASUREMENT EPOCH         = VALID
PC1-C0B RESULT            = PASS
TERMINAL SAFE STATE       = PASS
```

## CPU-visible execution evidence

The passive DMA trace established the required sequence:

| Index | Address | Type | ROM response | Meaning |
|---:|---:|---|---:|---|
| 00 | `FFFF0` | memory read, word | `00EA` | far-jump opcode and first operand byte |
| 01 | `FFFF2` | memory read, word | `0000` | remaining offset and segment low byte |
| 02 | `FFFF4` | memory read, word | `90F0` | segment high byte plus padding |
| 03 | `FFFF6` | memory read, word | high-Z | permitted prefetch; matcher does not advance |
| 04 | `F0000` | memory read, word | high-Z sentinel | far-jump target reached |
| 05 | `F0002` | memory read, word | high-Z | sequential target prefetch |

The first-cycle phase snapshots showed `00EA` continuously at R2, F2, and R3.
This closes the loop between address qualification, physical AD drive, V30
instruction consumption, and control transfer to `F0000`.

## Matcher trace interpretation

The matcher's four-word diagnostic RX FIFO retained:

```text
FFFF0  09C6DF3F  expected 09C6DF3F  YES
FFFF2  09CEDF3F  expected 09CEDF3F  YES
FFFF4  09C6FF3F  expected 09C6FF3F  YES
FFFF6  09CEFF3F  expected 01840322  NO
```

The final `NO` is expected and is useful qualification evidence. After the
three reset-ROM hits, the matcher held the `F0000` key across the unrelated
`FFFF6` prefetch. The diagnostic FIFO was then full, but the responder FIFO
count and independent DMA trace prove that the later `F0000` cycle matched and
consumed the fourth high-Z sentinel pair.

## Architecture conclusion

The earlier M33 lookup implementations correctly classified reset-ROM
addresses but missed the fixed READY-high response window. PC1-C0B passes only
after moving both current-cycle qualification and response scheduling into
PIO1. The accepted responsibility split is therefore:

```text
M33        policy, setup, and post-run reporting
DMA        passive bulk trace movement
PIO1       exact current-cycle qualification and AD timing
PIO0       passive observation
```

PC1-C0B validates the reset-vector transfer only. Serving executable ROM at
`F0000` remains the PC1-C0C boundary.
