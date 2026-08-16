# Performance Characterization 1 — Maximum Sustainable V30 Clock

## Purpose

Measure the maximum sustainable physical NEC V30 clock of the current RP2350 bus architecture before deeper BIOS/DOS expansion.

This characterization is deliberately separated from Gate 0–12 functional bring-up. The completed gate sequence proves bus semantics and the minimum PC-compatible interrupt/timer path. Performance Characterization 1 asks a different question: how fast can the current host architecture sustain that behavior when the V30 clock is free-running rather than host-stepped?

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

Therefore the existing stepped-clock gates remain the last-known-good functional regression baseline, but they are not used to claim V30 MHz performance.

## Continuous-clock path

Performance Characterization 1 introduces a separate PIO clock program:

```text
perf_continuous_clk
    set CLK high
    set CLK low
    repeat continuously
```

The PIO state machine runs at twice the configured V30 frequency because one instruction emits each half-cycle.

This performance path is intentionally isolated from the validated stepped-clock implementation so that the Gate 12 baseline is not modified while the real-time architecture is characterized.

## Single-run sweep

Target:

```text
performance_characterization_1
```

One firmware execution automatically runs all configured points:

```text
1.00 MHz
2.00 MHz
2.50 MHz
3.00 MHz
4.00 MHz
4.77 MHz
6.00 MHz
8.00 MHz
```

Each point performs a complete reset/reinitialization sequence before starting the next frequency.

The test does not stop permanently at the first failure. Higher points receive a bounded diagnostic attempt so the final log shows the complete sweep and the failure pattern.

## Workload

The initial continuous-clock workload reuses the completed Gate 12 end-to-end dependency chain:

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

Example:

```text
>>> Starting point 6/8: 4.770 MHz configured V30 clock <<<

Configured V30 clock : 4.770 MHz
Clock mode           : continuous PIO free-running
...
RESULT @ 4.770 MHz = PASS
```

The wording **Configured V30 clock** is intentional.

A PIO divider calculation alone is not independent measurement of the physical CLK waveform. A scope or frequency-counter capture must verify the physical clock before a configured value is reported as a measured frequency.

## Result summary

The firmware prints all eight results together:

```text
1.000 MHz   PASS
2.000 MHz   PASS
2.500 MHz   PASS
3.000 MHz   PASS
4.000 MHz   PASS
4.770 MHz   PASS
6.000 MHz   FAIL
8.000 MHz   FAIL

Last known-good configured clock : 4.770 MHz
First failing configured clock    : 6.000 MHz
```

The example above illustrates the format only and is not a project result.

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

The observed first failure mode determines the optimization work rather than assuming PIO, DMA or CPU frequency is automatically the answer.

## Optimization decision order

If the current continuous architecture does not reach the desired operating point, investigate in this order:

1. direct SIO hot path and GPIO masks;
2. SRAM-resident critical code/data;
3. branch/dispatch reduction and lookup tables;
4. greater PIO ownership of deterministic bus timing;
5. DMA only where measurement demonstrates benefit;
6. repeat the same single-run sweep after each architectural change.

## PASS interpretation

A point is considered PASS only when the physical workload reaches the deterministic SUCCESS loop while preserving the complete Gate 12 interrupt/timer chain and final idle PIC state.

A reset fetch alone is not sufficient.

The characterization milestone itself remains open until hardware results are captured, the physical CLK is independently verified at relevant points, and the maximum sustainable frequency is documented.
