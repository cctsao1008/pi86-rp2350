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

Performance Characterization 1 is now split into two explicit phases.

### PC1-A — Continuous software-polling baseline

Purpose: establish the performance and failure behavior of the current free-running clock plus Cortex-M33 software-polling service architecture.

The first 1–8 MHz runs produced non-monotonic failures almost immediately after reset fetch. Observed failure addresses and even cycle directions became implausible, for example a reset-vector cycle later being decoded as a memory write. This pattern is treated as evidence of bus-phase/data-service races rather than a valid RP2350 performance ceiling.

PC1-A therefore extends the diagnostic sweep below 1 MHz to determine whether software polling becomes reliable at lower free-running rates.

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

Possible interpretations:

```text
low frequencies PASS, higher frequencies FAIL
  -> establishes a software-polling ceiling / transition region

0.100 MHz also FAILS
  -> remaining protocol/timing correctness problem in the continuous polling harness

non-monotonic pass/fail pattern
  -> polling race / insufficient deterministic phase ownership remains dominant
```

PC1-A is not the final architectural benchmark.

### PC1-B — PIO-timed bus engine

PC1-B moves critical V30 bus-phase ownership away from Cortex-M33 software edge polling and into RP2350 PIO.

Minimum intended responsibility split:

```text
PIO
  -> generate deterministic V30 CLK
  -> observe/own T-state timing
  -> detect ALE/T1
  -> capture phase-aligned bus-cycle state
  -> present deterministic events to the CPU-side service path

Cortex-M33
  -> memory / I/O / PIC / PIT semantics
  -> non-cycle-critical policy and backend work
```

The first PIO-timed implementation should be incremental. Do not add DMA merely because it is available. DMA or deeper PIO data-response ownership should be introduced only when PC1-B measurements show a specific remaining deadline or throughput problem.

PC1-B must rerun comparable characterization against:

```text
original Pi86 historical baseline  ~0.3 MHz
PC1-A software-polling baseline     measured by this project
primary target                      4.77 MHz
stretch target                      8 MHz class
```

## Continuous-clock path

The current diagnostic path uses a separate PIO clock program:

```text
perf_continuous_clk
    set CLK high
    set CLK low
    repeat continuously
```

The PIO state machine runs at twice the configured V30 frequency because one instruction emits each half-cycle.

This path is intentionally isolated from the validated stepped-clock implementation so that Gate 12 remains unchanged as the functional baseline.

## Workload

The continuous characterization workload reuses the completed Gate 12 end-to-end dependency chain:

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

This provides memory, I/O programming, interrupt acknowledge, PIC, PIT, stack-entry, ISR and return-path coverage in one bounded physical workload.

Gate 7/8/9R/10/11/12 targets remain independent functional regressions and are not replaced by the performance target.

## Logging rule

No per-bus-cycle USB logging is performed while a frequency point is running. Printing every transaction would perturb the workload and invalidate the timing experiment.

Before each point the firmware logs the requested point. After the clock is stopped it prints the result and evidence summary.

The wording **Configured V30 clock** is intentional.

A PIO divider calculation alone is not independent measurement of the physical CLK waveform. A scope or frequency-counter capture must verify the physical clock before a configured value is reported as a measured frequency.

Each failure also records the bounded failure category plus failing address, cycle type and lane when available.

## Result interpretation

A point is considered PASS only when the physical workload reaches the deterministic SUCCESS loop while preserving the complete Gate 12 interrupt/timer chain and final idle PIC state.

A reset fetch alone is not sufficient.

For PC1-A, the important outputs are:

```text
last known-good software-polling configured clock
first failing software-polling configured clock
failure pattern around the transition
```

The result must not be described as the final RP2350 ceiling because PC1-B changes the real-time architecture.

## Failure interpretation

The first unstable point is evidence, not merely a failed test. The implementation records a bounded failure reason such as:

- ALE timeout / host missed bus timing;
- control-phase timeout;
- unsupported or corrupted bus-cycle decode;
- memory transaction failure;
- I/O transaction failure;
- PIC sequencing failure;
- INTA failure;
- PIT/IRQ0 routing failure;
- SUCCESS not reached before the cycle limit.

The first two hardware runs of the polling implementation showed non-monotonic failures and implausible addresses/cycle types. They are retained as architecture evidence but are not accepted as `max V30 clock < 1 MHz`.

## Optimization decision order

After PC1-A is captured, the next architectural action is PC1-B rather than further ad-hoc polling delay tuning.

Within PC1-B, investigate in this order:

1. PIO ownership of deterministic bus-phase capture;
2. direct SIO hot path and GPIO masks where CPU-side service remains timing-critical;
3. SRAM-resident critical code/data;
4. branch/dispatch reduction and lookup tables;
5. greater PIO ownership of response timing if measurements require it;
6. DMA only where measurement demonstrates benefit;
7. rerun comparable characterization after each architectural change.

## Milestone completion

Performance Characterization 1 remains open until:

1. PC1-A software-polling baseline is captured and documented;
2. PC1-B PIO-timed bus engine is implemented and characterized;
3. physical CLK is independently verified at the relevant operating points; and
4. the maximum sustainable validated frequency of the chosen architecture is documented.
