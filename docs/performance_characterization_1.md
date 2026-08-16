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

#### PC1-A methodological defect — reset was not normalized in clock cycles

PC1-A used a fixed `RESET_SETTLE_US = 50 us` while the V30 clock frequency changed across the sweep. Therefore the RESET-high duration, expressed in V30 clocks, was not held constant:

```text
0.100 MHz -> 5 clocks
0.200 MHz -> 10 clocks
0.300 MHz -> 15 clocks
0.500 MHz -> 25 clocks
0.750 MHz -> 37.5 clocks
1.000 MHz -> 50 clocks
2.000 MHz -> 100 clocks
2.500 MHz -> 125 clocks
3.000 MHz -> 150 clocks
4.000 MHz -> 200 clocks
4.770 MHz -> 238.5 clocks
6.000 MHz -> 300 clocks
8.000 MHz -> 400 clocks
```

The NEC V20/V30 reset contract requires RESET to remain active for at least 4 clocks. The 0.100 MHz point therefore operated only one clock above the minimum, while the 8.000 MHz point held RESET for 400 clocks.

This means the 13 points were not a controlled frequency-only experiment: reset history varied by 80x across the sweep. As a result, PC1-A cannot methodologically support a throughput-ceiling claim even if the failures had been monotonic. This is a defect in the PC1-A experiment design and is recorded as such, not merely as a PC1-B improvement opportunity.

#### PC1-A failure fingerprint

The failure-address distribution also contains a useful falsifiable fingerprint.

Among the ten captured nonzero failure addresses, nine have an upper hex nibble in `{0,1,2,3}`. The observed distribution is approximately:

```text
2xxxx  x6
1xxxx  x2
3xxxx  x1
```

This pattern is consistent with repeated capture of post-T1 processor-status values in the A19..A16 pins rather than a true physical address high nibble. In the NEC V30 bus contract, A19..A16 carry address during T1 and processor status afterward. Interpreting later-phase status as address therefore creates apparently structured but bogus addresses instead of random noise.

This fingerprint is intentionally retained as a PC1-B discriminator:

```text
If PC1-B failing addresses begin showing the expected Fxxxx reset/fetch region,
then T1 capture timing has materially improved.

If PC1-B still clusters in 0xxxx/1xxxx/2xxxx/3xxxx,
then moving capture into PIO has not fixed the root cause and the next investigation
must focus on exactly which phase is entering the capture FIFO.
```

This is more useful than a generic `protocol/phase failure` label because it is falsifiable against the next architecture revision.

#### PC1-A interpretation

PC1-A is **not** accepted as a measured performance ceiling of `< 0.100 MHz`.

Three independent reasons make a throughput-ceiling interpretation invalid:

1. even 0.100 MHz fails, despite a 10 us clock period;
2. failures are non-monotonic and include bogus addresses / wrong cycle directions / intermittent reset-vector detection; and
3. RESET duration was not normalized in clock cycles, so frequency was not the only changing independent variable.

The observed `memory transaction failure` is therefore treated primarily as a downstream symptom of incorrect bus-phase observation or response, not as a root cause in the memory backend.

PC1-A is complete as architecture/protocol diagnostic evidence. Do not spend further work tuning arbitrary polling delays to force a pass.

### PC1-B — PIO-timed bus engine

PC1-B moves critical V30 bus-phase ownership away from Cortex-M33 software edge polling and into RP2350 PIO.

The first implementation must correct two classes of nondeterminism exposed by PC1-A:

1. RESET sequencing must be expressed in clock counts and released on a deterministic V30 clock phase.
2. T1/ALE and later control/data sampling must be captured by PIO at deterministic hardware phases rather than by Cortex-M33 polling loops.

Minimum intended responsibility split:

```text
PIO clock/reset state machine
  -> generate deterministic V30 CLK
  -> hold RESET for a defined integer number of V30 clocks
  -> release RESET on a defined clock phase

PIO bus-capture state machine
  -> detect ALE/T1
  -> capture raw T1 GPIO state
  -> capture phase-aligned control/write-data state
  -> include clock-count / phase metadata
  -> provide deterministic FIFO events

Cortex-M33
  -> decode captured snapshots
  -> memory / I/O / PIC / PIT semantics
  -> drive read/vector data through the SIO hot path
  -> backend policy
```

#### PC1-B state-machine synchronization rule

The clock/reset SM and bus-capture SM must not be enabled in separate software operations if their relative phase matters.

They must be initialized disabled, then started by one PIO control-register write that sets both required `SM_ENABLE` bits together. Otherwise startup ordering becomes a new run-to-run phase variable and merely moves nondeterminism from the Cortex-M33 polling loop into PIO startup.

#### PC1-B entry gate — do not start with a sweep

The first PC1-B hardware gate is a **single-point functional validation at 0.300 MHz**.

Do not run the full characterization sweep until the following end-to-end path has passed once at 0.300 MHz:

```text
RESET held for defined N clocks
  -> deterministic RESET release
  -> FFFF0 reset fetch captured in T1
  -> ROM execution
  -> PIC programming
  -> PIT programming
  -> terminal count
  -> IRQ0 / INTR
  -> INTA #1 / #2
  -> vector 20h
  -> IVT / ISR / marker / EOI / IRET
  -> SUCCESS
```

Rationale: PC1-A spent 13 measurements repeating essentially the same early failure without first establishing a known-good continuous-clock point. PC1-B must establish one known-good functional point before frequency becomes an experimental variable.

Only after 0.300 MHz passes should the automated sweep be enabled.

#### PC1-B failure ring buffer

PC1-B must retain a bounded diagnostic ring buffer containing the final N events before PASS/FAIL. At minimum each entry should include:

```text
raw T1 GPIO snapshot
raw control/data-phase GPIO snapshot
clock count since RESET assertion or release
classified cycle type
captured lane state
```

The buffer is printed only after the timing-critical run has stopped. No per-cycle USB logging is permitted during execution.

The ring buffer exists to preserve the evidence immediately preceding loss of synchronization. A final label such as `memory transaction failure` is insufficient by itself.

#### PC1-B clock self-measurement

PC1-B should not rely only on the configured PIO divider.

The firmware must include an internal clock-measurement mechanism that counts actual V30 CLK edges over a defined RP2350 reference-time interval or equivalent deterministic reference window. The log should report both:

```text
Configured V30 clock
Measured V30 clock
```

Once internal measurement has been validated, the project no longer needs the standing caveat that configured frequency is not measured frequency for routine characterization. External scope verification remains useful for waveform quality, duty cycle and timing-margin investigations, but not as the sole frequency truth source.

#### PC1-B read-response latency instrumentation

PC1-B must instrument the likely next binding constraint before optimization begins.

For each memory or I/O read response, capture at least the latency from the deterministic T2/control event to the point where response data is actually driven on AD. Report a bounded statistic such as:

```text
minimum response latency
maximum response latency
worst-case response latency in RP2350 cycles
worst-case response latency converted to ns
```

This makes a later 4.77 MHz or 8 MHz failure quantitatively actionable. If the read-data deadline is missed, the project should know how much latency must be removed before selecting SIO hot-path, SRAM placement, deeper PIO response ownership or DMA work.

#### PC1-B optimization order

The first PIO-timed implementation remains incremental. Do not add DMA by default.

Use this order:

1. deterministic PIO clock/reset sequencing;
2. simultaneous SM startup where phase coupling is required;
3. PIO ownership of T1/control phase capture;
4. ring-buffer evidence and internal clock measurement;
5. read-response latency instrumentation;
6. direct SIO hot path and GPIO masks where CPU-side response remains timing-critical;
7. SRAM-resident critical code/data;
8. branch/dispatch reduction and lookup tables;
9. greater PIO ownership of response timing if measurements require it;
10. DMA only where measurement demonstrates benefit;
11. rerun comparable characterization after each architectural change.

PC1-B must rerun comparable characterization against:

```text
original Pi86 historical baseline  ~0.3 MHz
PC1-A software-polling result       protocol/methodology FAIL at 0.100–8.000 MHz
primary target                      4.77 MHz
stretch target                      8 MHz class
```

## V30 timing contract relevant to PC1-B

The NEC V20/V30 bus model uses T1/T2/T3/T4 states. Address is valid during T1; the multiplexed AD bus is used for data in later states. A19..A16 similarly represent address during T1 and processor status later in the bus cycle. RESET is active high and requires at least 4 clocks before release. The physical reset-vector dependency remains `FFFF0h`.

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

PC1-A logs retain the wording **Configured V30 clock** because PC1-A did not include internal self-measurement.

PC1-B should report both configured and internally measured clock values once the self-measurement path is validated. Failure evidence must retain bounded failure category plus the final diagnostic ring-buffer entries.

## Result interpretation

A point is considered PASS only when the physical workload reaches the deterministic SUCCESS loop while preserving the complete Gate 12 interrupt/timer chain and final idle PIC state. A reset fetch alone is not sufficient.

PC1-A result:

```text
Last known-good polling clock : none
First failing polling clock    : 0.100 MHz
Interpretation                 : protocol/phase synchronization + experiment-control defect,
                                 not RP2350 performance ceiling
```

## PC1-B entry criteria / gate checklist

PC1-B may begin implementation when all items below are explicit in the design:

- [ ] RESET hold duration is specified in V30 clock counts, not microseconds.
- [ ] RESET release phase is deterministic.
- [ ] Clock/reset and capture state machines that require phase alignment are initialized disabled and enabled simultaneously.
- [ ] T1 capture is owned by PIO and preserves raw GPIO evidence.
- [ ] Control/write-data capture is tied to a defined V30 phase and preserves raw GPIO evidence.
- [ ] A bounded failure ring buffer is defined.
- [ ] A clock-count field is attached to capture evidence.
- [ ] Internal V30 clock self-measurement is defined.
- [ ] T2-to-data-driven response latency instrumentation is defined.
- [ ] The first hardware gate is a single 0.300 MHz end-to-end functional PASS, not a sweep.
- [ ] The Gate 12 stepped-clock target remains unchanged as the last-known-good regression baseline.
- [ ] DMA is explicitly excluded from the first PC1-B implementation unless later measurement justifies it.

Only after the 0.300 MHz entry gate passes should the full performance sweep be enabled.

## Milestone completion

Performance Characterization 1 remains open until:

1. PC1-A software-polling baseline is captured and documented — **DONE**;
2. PC1-B PIO-timed bus engine passes the 0.300 MHz single-point functional gate;
3. PC1-B is characterized across the planned frequency range;
4. internal clock self-measurement is validated, with external scope checks used where needed for waveform/timing quality; and
5. the maximum sustainable validated frequency of the chosen architecture is documented.
