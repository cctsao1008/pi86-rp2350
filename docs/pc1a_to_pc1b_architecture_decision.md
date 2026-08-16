# PC1-A to PC1-B Architecture Decision

## Status

Decision: stop further PC1-A per-cycle M33 polling micro-optimization and begin a PC1-B deterministic PIO timing prototype.

This decision is based on physical V30 + RP2350-PiZero + Pi86 HAT measurements at 0.300 MHz and 0.600 MHz. It is an architecture decision about the current servicing model, not a statement that RP2350 itself cannot operate a V30 at these or higher clock rates.

## Evidence

### Continuous clock and raw bus generation

Passive, host-high-Z traces showed clean reset prefetch progression:

- 0.300 MHz: `FFFF0 -> FFFF2 -> FFFF4 -> FFFF6`
- 0.600 MHz: `FFFF0 -> FFFF2 -> FFFF4 -> FFFF6`

The 0.600 MHz ALE-to-ALE spacing was approximately half the 0.300 MHz spacing, consistent with a four-clock V30 bus cycle. This validates the continuous PIO clock generator and the raw bus-cycle generation layer at these two points.

### T1 capture correction

The raw traces also confirmed that A19:A16 carry the high address nibble during ALE/T1 and then change after ALE falls. This explains the invalid PC1-A Rev0 `2FFF0` observations as late high-address sampling rather than a real V30 address.

### Single-read timing window

The read-response matrix found a repeatable electrical candidate at D2-H2:

- D2: after ALE falls, wait for the next fresh CLK falling edge before asserting AD output.
- H2: hold the driven word through two subsequent fresh CLK falling edges, then release AD to high-Z.

At 0.300 MHz, D2-H2 allowed `00EA` to become visible on the AD pads while a passive PIO observer still saw `FFFF0 -> FFFF2 -> FFFF4`.

This demonstrates a usable single-transaction response window. It does not by itself prove sustainable back-to-back servicing.

### Back-to-back service failure

A full reset-vector D2-H2 service test failed because M33 reacquisition missed bus cycles. A fully unrolled hot-pipeline variant removed decode, address checks, logging, pad reads, memory lookup, PIC, and PIT work from the hot path, leaving only ALE acquisition, edge waits, AD drive, and release.

The decisive result was:

```text
M33 raw T1 decode = FFFF0 / FFFF4 / FFFF6
PIO ALE sequence  = FFFF0 / FFFF2 / FFFF4 / FFFF6
```

The passive PIO observer captured every bus cycle while M33 polling still missed `FFFF2` at only 0.300 MHz.

Therefore the current per-cycle M33 polling transaction model is not demonstrated sustainable at 0.300 MHz. Further C-level micro-optimization is not justified for a design whose meaningful target begins at 1 MHz and whose primary target is 4.77 MHz.

## Decision

PC1-A polling optimization stops here.

The next architecture, PC1-B, moves cycle/phase detection into PIO while keeping higher-level address/data/peripheral semantics on M33 where possible.

Initial split:

```text
V30 CLK/ALE
    |
    v
PIO deterministic timing front-end
    |
    | phase event
    v
M33 SIO response
    |
    v
AD15:0
```

This is intentionally a minimal intermediate architecture. It does not yet commit the project to a complete PIO bus engine.

## PC1-B first discriminator

The first PC1-B prototype shall:

1. Keep the V30 clock free-running at 0.300 MHz.
2. Use PIO to detect ALE turnover and generate deterministic D2 and H2 timing events.
3. Remove M33 polling of ALE/CLK from the read-response path.
4. Keep AD driving on M33/SIO because the physical Pi86 HAT maps AD15:0 to non-contiguous RP2350 GPIO pins.
5. Service only the reset-vector words `00EA`, `0000`, and `90F0` initially.
6. Use the passive PIO ALE observer as the independent control-flow discriminator.

The decisive PASS remains:

```text
FFFF0 -> FFFF2 -> FFFF4 -> F0000
```

If PIO-triggered M33/SIO response succeeds, PC1-B can evolve toward a deterministic PIO timing front-end with M33 semantics. If PIO-to-M33 event latency still cannot meet the physical read window, the next step is a more hardware-assisted data path such as PIO/DMA/precomputed GPIO response rather than returning to per-cycle M33 polling.

## Guardrails

- Canonical Gate 0-12 results remain unchanged.
- PC1-A diagnostics remain as evidence and are not rewritten into PASS results.
- `D2-H2` is an experimentally observed candidate on the current physical setup, not a V30 datasheet timing specification.
- Do not claim a sustainable 0.300 MHz bus-service rate until the complete reset-vector control-flow discriminator passes.
- Do not extrapolate 0.300/0.600 MHz raw clock success to 1/2/4.77/8 MHz service capability without physical validation.
