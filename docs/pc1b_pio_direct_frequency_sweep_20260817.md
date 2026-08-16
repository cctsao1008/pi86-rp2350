# PC1-B PIO-Direct Frequency Sweep — 2026-08-17

## Summary

PC1-B PIO-direct post-reset self-loop characterization passed at every tested frequency from 0.300 MHz through 8.000 MHz on physical NEC V30 hardware.

The validated critical path is:

```text
V30 bus phase -> PIO1 direct AD/PINDIRS control
DMA -> PIO1 TX FIFO only
M33 not in the per-cycle critical response path
```

The previous DMA->SIO response architecture is not used in this path.

## Test configuration

- RESET qualification: repeated clock-only epoch at every frequency point
- Measurement epoch: post-reset controlled and validated
- Read-response timing: AF-H2
- Data path: PIO1 owns AD data and PINDIRS
- Instruction at reset vector: `EB FE` (`JMP $`)
- Observer/phase capture: passive PIO0 with default input synchronizers
- Sweep policy: execute every point; do not stop after a failure
- Canonical gate behavior remains unchanged

## Frequency results

| V30 clock | Epoch | First ALE | DMA -> PIO | Self-loop | Result |
|---:|---|---|---|---|---|
| 0.300 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 0.600 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 1.200 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 2.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 3.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 4.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 5.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 6.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 7.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |
| 8.000 MHz | VALID | FFFF0 | PASS | PASS | PASS |

## Control-flow evidence

At all tested points, the V30 produced a self-loop control-flow signature such as:

```text
FFFF0 -> FFFF2 -> FFFF4 -> FFFF0
```

The repeated `FFFF0` after the initial reset-vector fetch is the CPU-side discriminator that `EB FE` was consumed and executed.

This is stronger evidence than GPIO activity alone: it proves the data driven by PIO1 was sampled by the V30 and changed instruction execution.

## First-cycle physical GPIO evidence

Typical first-cycle snapshots from 0.300-6.000 MHz and 8.000 MHz were:

```text
AF  = FFF0
R1  = FFF0
F1  = FEE0
R2  = FEEB
F2  = FEEB
R3  = FEEB
```

This indicates the AD bus turnaround is in progress around F1, while the complete response word `FEEB` is physically present by R2 and remains valid through F2/R3 for the tested configuration.

At 7.000 MHz, F1 was observed as `FFF3` while R2/F2/R3 were still `FEEB` and the self-loop discriminator passed. Therefore F1 is treated as a transitional snapshot, not a guaranteed data-valid point.

## Architectural conclusion

The validated PC1-B timing front-end is:

```text
PIO0:
  - continuous V30 clock
  - passive ALE observation / diagnostics

PIO1:
  - deterministic bus-phase detection
  - direct AD output
  - direct PINDIRS ownership/release

DMA:
  - feeds PIO TX FIFO

M33:
  - policy/device-model work outside the cycle-critical GPIO response
```

The result establishes that the PIO-direct read-response microarchitecture is viable through at least 8.000 MHz for this fixed/preloaded self-loop discriminator.

## Scope / limitations

This result does **not** yet prove a complete 8 MHz Pi86 bus engine. The following remain to be validated under continuous clock:

- exact 4.770 MHz point
- far jump / multi-word executable ROM
- arbitrary address-dependent ROM reads
- RAM reads and writes
- byte lanes and odd-address word cycles
- qualified memory-read ownership
- I/O reads/writes
- INTA / PIC / PIT integration
- sustained FIFO/DMA service without starvation
- continuous-clock integrated equivalent of the earlier semantic gates

## Next recommended validation

PC1-B has answered the fixed-response speed question. The next milestone is PC1-C address-qualified ROM execution, not a denser fixed-response frequency sweep.

1. Decode the reset-vector memory read at `FFFF0` and return a far jump (`EA 00 00 00 F0`).
2. Serve the destination stream from SRAM-backed ROM at `F0000`.
3. Prove execution with a CPU-visible checkpoint, then add debug port `0xE9` output of `OK`.
4. Sweep the address-qualified implementation only after the semantic test passes, including an exact 4.770 MHz point.
5. Extend the continuous-clock front end to qualified writes, byte lanes, I/O, INTA, PIC, and PIT behavior.

## Milestone statement

> PC1-B PIO-direct read-response timing front-end validated from 0.300 MHz through 8.000 MHz on physical NEC V30 hardware. A post-reset `EB FE` self-loop was correctly consumed and executed at every tested frequency. This establishes PIO-direct GPIO ownership with DMA-fed PIO FIFO as the viable deterministic bus-response architecture. Full address-dependent memory/I/O service remains to be validated.
