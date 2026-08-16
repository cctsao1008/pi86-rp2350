# Performance Characterization 1 — Maximum Sustainable V30 Clock

## Purpose

Measure the maximum sustainable physical NEC V30 clock of the RP2350 host architecture before deeper BIOS/DOS expansion.

This characterization is deliberately separated from Gate 0–12 functional bring-up. The completed gate sequence proves bus semantics and the minimum PC-compatible interrupt/timer path. Performance Characterization 1 asks a different question: how fast can the host sustain that behavior when the V30 clock is free-running rather than host-stepped?

## Historical comparison

The original Pi86 project reports an approximately 0.3 MHz physical processor operating point. That value is the historical comparison baseline, not the target limit for `pi86-rp2350`.

Interpretation targets:

| V30 clock | Interpretation |
|---:|---|
| ~0.3 MHz | Original Pi86 comparison baseline |
| >= 1 MHz | Minimum meaningful improvement |
| >= 2 MHz | Major improvement |
| 4.77 MHz | Primary IBM PC-class target |
| 8 MHz class | Stretch target |

## Why the Gate 0–12 clock cannot be used as the performance metric

The bring-up gates use `gate4_step_clock.pio`, where each host call advances one clock pulse and the PIO stalls low until the Cortex-M33 requests the next step.

That mechanism is intentionally slow and observable. `STEP_PIO_CLOCK_HZ` is the PIO state-machine instruction frequency, not the physical V30 clock frequency.

Therefore the stepped-clock gates remain the last-known-good functional regression baseline, but they are not used to claim V30 MHz performance.

## Two-phase characterization strategy

Performance Characterization 1 is split into two explicit phases.

### PC1-A — Continuous software-polling baseline

Purpose: characterize the failure behavior of the free-running clock plus Cortex-M33 software-polling service architecture before moving deterministic phase ownership into PIO.

Target:

```text
performance_characterization_1_polling_baseline
```

Single-run sweep:

```text
0.100 MHz
0.200 MHz
0.300 MHz
0.500 MHz
0.750 MHz
1.000 MHz
2.000 MHz
2.500 MHz
3.000 MHz
4.000 MHz
4.770 MHz
6.000 MHz
8.000 MHz
```

The low-frequency points are diagnostic, not project targets. In particular, 0.300 MHz is included because it is approximately the historical Pi86 comparison point.

#### PC1-A physical result — diagnostic FAIL at all 13 points

The completed hardware sweep produced:

```text
0.100 MHz   FAIL   memory transaction failure
0.200 MHz   FAIL   ALE timeout / host missed bus timing
0.300 MHz   FAIL   ALE timeout / host missed bus timing
0.500 MHz   FAIL   memory transaction failure
0.750 MHz   FAIL   ALE timeout / host missed bus timing
1.000 MHz   FAIL   memory transaction failure
2.000 MHz   FAIL   memory transaction failure
2.500 MHz   FAIL   memory transaction failure
3.000 MHz   FAIL   memory transaction failure
4.000 MHz   FAIL   memory transaction failure
4.770 MHz   FAIL   memory transaction failure
6.000 MHz   FAIL   memory transaction failure
8.000 MHz   FAIL   memory transaction failure
```

No frequency reached the Gate 12 end-to-end SUCCESS condition.

Representative evidence includes:

```text
0.100 MHz: first failing cycle address=20164h, MEM_READ, WORD
0.200 MHz: reset-vector observed, then ALE timeout after 2 serviced cycles
0.300 MHz: reset-vector observed, then ALE timeout after 2 serviced cycles
1.000 MHz: failing address=2FFFCh decoded as MEM_WRITE
4.770 MHz: failing address=2001Ah after 6 serviced cycles
8.000 MHz: failing address=32000h after 4 serviced cycles
```

#### Interpretation

This is **not** accepted as a measured performance ceiling of `< 0.100 MHz`.

The important result is that even 0.100 MHz fails and the failure pattern remains non-monotonic. At 0.100 MHz the clock period is 10 us, so the failure cannot reasonably be explained only as Cortex-M33 compute throughput exhaustion. The harness is still losing deterministic V30 bus-phase/protocol alignment under a free-running clock.

The observed bogus addresses, wrong cycle directions and intermittent reset-vector detection show that software observation and response are not reliably synchronized to the V30 T-state contract. PC1-A therefore establishes an **architecture/protocol baseline**, not a useful maximum clock number.

PC1-A is now considered complete as diagnostic evidence. Do not spend further work tuning arbitrary polling delays to force a pass.

### PC1-B — PIO-timed bus engine

PC1-B moves critical V30 bus-phase ownership away from Cortex-M33 software edge polling and into RP2350 PIO.

The first implementation must correct two classes of nondeterminism exposed by PC1-A:

1. RESET release must be tied to a deterministic clock phase rather than occurring at an arbitrary point in a free-running cycle.
2. T1/ALE and later control/data sampling must be captured by PIO at deterministic hardware phases rather than by Cortex-M33 polling loops.

Minimum intended responsibility split:

```text
PIO clock/reset state machine
  -> generate deterministic V30 CLK
  -> hold RESET for a defined number of clocks
  -> release RESET on a defined clock phase

PIO bus-capture state machine
  -> detect ALE/T1
  -> capture raw T1 GPIO state
  -> capture phase-aligned control/write-data state
  -> provide deterministic FIFO events

Cortex-M33
  -> decode captured snapshots
  -> memory / I/O / PIC / PIT semantics
  -> drive read/vector data through the SIO hot path
  -> backend policy
```

The first PIO-timed implementation remains incremental. Do not add DMA by default. If deterministic capture works but read-data response still misses the V30 data window, that becomes the next measured boundary: direct SIO/SRAM hot-path optimization first, deeper PIO response ownership or DMA only when evidence requires it.

PC1-B must rerun comparable characterization against:

```text
original Pi86 historical baseline  ~0.3 MHz
PC1-A software-polling result       protocol FAIL at 0.100–8.000 MHz
primary target                      4.77 MHz
stretch target                      8 MHz class
```

## V30 timing contract relevant to PC1-B

The NEC V20/V30 bus model uses T1/T2/T3/T4 states. Address is valid during T1; the multiplexed AD bus is used for data in later states. RESET is active high and must be held for multiple clocks before release. The physical reset-vector dependency remains `FFFF0h`.

PC1-B should treat the NEC timing contract as normative and use the already validated Gate 0–12 hardware behavior as the project regression reference.

## Continuous-clock path

The PC1-A diagnostic path uses a separate PIO clock program:

```text
perf_continuous_clk
    set CLK high
    set CLK low
    repeat continuously
```

The PIO state machine runs at twice the configured V30 frequency because one instruction emits each half-cycle.

This path remains preserved as diagnostic evidence. It is not the final bus engine.

## Workload

Comparable characterization should reuse the completed Gate 12 end-to-end dependency chain:

```text
RESET / reset-vector fetch
  -> ROM execution
  -> PIC ICW1-4 / IMR programming
  -> PIT channel-0 programming through OUT 43h / 40h
  -> PIT terminal count
  -> pi86_pic IRQ0
  -> INTR
  -> INTA #1 / #2
  -> vector 20h
  -> IVT
  -> ISR0
  -> marker
  -> EOI
  -> IRET
  -> SUCCESS
```

Gate 7/8/9R/10/11/12 targets remain independent functional regressions and are not replaced by the performance target.

## Logging rule

No per-bus-cycle USB logging is performed while a frequency point is running.

The wording **Configured V30 clock** remains intentional. A PIO divider calculation alone is not independent measurement of the physical CLK waveform. A scope or frequency-counter capture must verify the physical clock before a configured value is reported as a measured frequency.

Failure evidence should retain bounded failure category plus failing address, cycle type and lane where available.

## Result interpretation

A point is considered PASS only when the physical workload reaches the deterministic SUCCESS loop while preserving the complete Gate 12 interrupt/timer chain and final idle PIC state. A reset fetch alone is not sufficient.

PC1-A result:

```text
Last known-good polling clock : none
First failing polling clock    : 0.100 MHz
Interpretation                 : protocol/phase synchronization failure,
                                 not RP2350 performance ceiling
```

## Optimization decision order

After the recorded PC1-A result, the next architectural action is PC1-B rather than further ad-hoc polling delay tuning.

Within PC1-B, investigate in this order:

1. deterministic PIO clock/reset sequencing;
2. PIO ownership of T1/control phase capture;
3. direct SIO hot path and GPIO masks where CPU-side response remains timing-critical;
4. SRAM-resident critical code/data;
5. branch/dispatch reduction and lookup tables;
6. greater PIO ownership of response timing if measurements require it;
7. DMA only where measurement demonstrates benefit;
8. rerun comparable characterization after each architectural change.

## Milestone completion

Performance Characterization 1 remains open until:

1. PC1-A software-polling baseline is captured and documented — **DONE**;
2. PC1-B PIO-timed bus engine is implemented and characterized;
3. physical CLK is independently verified at the relevant operating points; and
4. the maximum sustainable validated frequency of the chosen architecture is documented.
