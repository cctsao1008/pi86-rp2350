# Performance Characterization 1 — Maximum Sustainable V30 Clock

> **Document status: HISTORICAL CHARACTERIZATION PLAN AND RECORD.**
>
> The original software-polling path below is retained as experimental
> history. PC1-B subsequently validated the PIO-direct fixed-response
> architecture from 0.300 through 8.000 MHz on physical V30 hardware.
> Address-qualified and bounded ROM/RAM work later continued through PC1-C;
> this document is no longer the current roadmap.
>
> Canonical current architecture: [`architecture.md`](architecture.md).
> Retained physical evidence:
> [`validation/pc1b_pio_direct_frequency_sweep.md`](validation/pc1b_pio_direct_frequency_sweep.md),
> [`validation/pc1c0c1b1_bounded_rom_validation.md`](validation/pc1c0c1b1_bounded_rom_validation.md), and
> [`validation/pc1c0c1b2c_multi_slot_ram_validation.md`](validation/pc1c0c1b2c_multi_slot_ram_validation.md).

## Purpose

Measure the maximum sustainable physical NEC V30 clock of the RP2350 host architecture before deeper BIOS/DOS expansion.

Gate 0–12 prove functional bus semantics and the minimum PC-compatible interrupt/timer path. Performance Characterization 1 asks a separate question: how fast can the host sustain that behavior with a free-running V30 clock?

## Historical comparison

The original Pi86 project reports approximately 0.3 MHz physical processor operation. This is a historical comparison baseline, not the project target.

| V30 clock | Interpretation |
|---:|---|
| ~0.3 MHz | Original Pi86 comparison baseline |
| >= 1 MHz | Minimum meaningful improvement |
| >= 2 MHz | Major improvement |
| 4.77 MHz | Primary IBM PC-class target |
| 8 MHz class | Stretch target |

## Known-good functional reference

The Gate 0–12 implementation uses a host-stepped PIO clock. Each `v30_bus_step()` emits one complete HIGH→LOW pulse and returns the GPIO snapshot only after the low phase. This implementation is intentionally slow but deterministic and has passed physical hardware validation through Gate 12.

Gate 12 remains the last-known-good functional regression baseline:

```text
V30 OUT 43h / 40h
  -> PIT channel 0
  -> terminal count
  -> pi86_pic IRQ0
  -> INTR
  -> INTA #1 / #2
  -> vector 20h
  -> IVT
  -> ISR
  -> marker
  -> EOI
  -> IRET
  -> SUCCESS
```

The stepped clock is not a MHz performance metric.

---

## PC1-A Rev0 — INVALID characterization

Target used:

```text
performance_characterization_1_polling_baseline
```

The Rev0 harness used a free-running PIO clock plus Cortex-M33 software polling and ran a 13-point sweep from 0.100 to 8.000 MHz. Every point failed before the Gate 12 end-to-end SUCCESS condition.

Those results are retained as implementation-defect evidence, but **Rev0 is invalid as a polling-performance characterization and must not be used to claim either a software-polling ceiling or an RP2350 performance ceiling.**

### Root cause: T1/address sampled at the wrong end of T1

The Rev0 continuous harness accepted the first GPIO sample where ALE was observed high:

```c
wait_signal_level(V30_PIN_ALE, true, &t1);
```

That samples near the beginning of ALE-high / T1.

The known-good stepped engine samples after a complete HIGH→LOW pulse, near the end of T1 / ALE falling region. The two engines therefore sampled opposite ends of the T1 address window.

The V20/V30 bus timing contract does not guarantee that the full address has propagated at the first observation of ALE high. Conventional 8282/8283-style latching is transparent while ALE is high and retains the address on the falling transition. Rev0 therefore sampled at a phase the device does not guarantee as address-valid.

This is a **protocol/timing implementation defect**.

### Strongest Rev0 evidence

The strongest evidence is not the overall histogram but two individual captures.

#### Cycle-zero corruption

At 4.000 MHz the first captured bus cycle reported:

```text
observed: 2FFF0 / MEM_WRITE / WORD
serviced bus cycles before failure: 0
```

The first bus cycle after RESET must be the reset-vector fetch:

```text
expected: FFFF0 / MEM_READ / WORD
```

No instruction had executed, so this cannot be downstream CPU derailment. Both address and direction were corrupted at capture time. This also means the Rev0 control sample is **not established as correct** and must be re-derived from a corrected T1 anchor.

#### `AFFF4` capture

A Rev0 failure also captured `AFFF4` in the reset-vector region, where `FFFF4` is expected. The intact low 16 bits with a mixed high nibble are strongly consistent with A19..A16 being sampled during a transition rather than at a valid T1 address point.

### Failure-nibble fingerprint — corrected statistical interpretation

The previous wording treated `{0,1,2,3}` failures as if the failure list were an unbiased address sample. It is not.

The memory map itself creates selection bias:

```text
0xxxx  -> mapped RAM; a corrupted address can be serviced silently
Fxxxx  -> mapped ROM; a corrupted address can be serviced silently
1xxxx-E​xxxx -> unmapped and therefore observable as memory-transaction failure
```

The correct supporting observation is:

```text
observable unmapped upper-nibble space: 1..E = 14 bins
9 of 10 recorded nonzero failures fall in {1,2,3}
```

with the approximate distribution:

```text
2xxxx  x6
1xxxx  x2
3xxxx  x1
```

This is consistent with status-related A19..A16 corruption and with the workload's dominant CS/SS/DS activity, but it is supporting evidence rather than the primary proof.

The Rev1 ring buffer must preserve raw high-nibble evidence so this hypothesis remains falsifiable.

### Experiment-control defect: RESET duration varied by 80x

Rev0 held RESET high for a fixed 50 us while changing V30 frequency. Expressed in processor clocks:

```text
0.100 MHz -> 5 clocks
8.000 MHz -> 400 clocks
```

The V30 requires RESET for at least 4 clocks. Frequency was therefore not the only changing independent variable; reset history varied by 80x across the sweep.

This is an **experiment-method defect**. No throughput comparison across the Rev0 frequency points is admissible.

### RESET GPIO transition defect

Rev0 `hold_reset()` called `gpio_init()` on every RESET transition. This can temporarily return the pin to input before it is driven again, leaving an asynchronous RESET input briefly undriven with pulls disabled.

This is a correctness defect worth removing. It is not claimed as the cause of the Rev0 failure pattern.

### Additional uncontrolled variables found in review

Rev0 also contained the following confounders:

1. `MAX_SIGNAL_SPINS` was a fixed software-iteration timeout rather than a timeout normalized in V30 clocks.
2. Interrupts were not masked while the polling loop ran and USB stdio remained active.
3. Timing-critical service code executed from XIP flash rather than being placed in deterministic SRAM.
4. Several sweep points used fractional PIO clock dividers, introducing bounded divider jitter / duty asymmetry.
5. AD release timing had no measured guard margin before the next T1.
6. No independent ALE-cycle counter existed, so an entirely missed bus cycle could go undetected.

The first three must be controlled before Rev1 can be treated as a valid polling baseline. The remaining three must at least be instrumented and reported.

### Rev0 status

```text
PC1-A Rev0: INVALID CHARACTERIZATION

Useful for:
- identifying the T1/address sampling bug
- identifying control-phase uncertainty on cycle zero
- identifying RESET and timeout experiment-control defects
- preserving failure fingerprints

Not valid for:
- polling maximum clock
- RP2350 maximum clock
- proving that software polling is architecturally insufficient
- quantifying the benefit of PC1-B
```

The detailed review record is preserved in:

```text
docs/pc1a_rev0_review.md
```

---

## PC1-A Rev1 — Controlled software-polling baseline

PC1-A Rev1 must be completed before PC1-B is used for architectural comparison. Rev1 is not an attempt to rescue polling as the final architecture; it exists to produce a valid control group.

### Rev1 P0 — experiment control

Before any measurement:

- initialize RESET GPIO once and keep it configured as output;
- express RESET hold duration in a fixed integer number of V30 clocks for every point;
- release RESET at a defined V30 clock boundary;
- express ALE/CLK timeout budgets in V30 clocks rather than fixed loop iterations;
- mask interrupts for the timing-critical portion of each point;
- perform USB logging only outside timing-critical execution;
- place the timing-critical polling/service path and required hot data in SRAM so XIP cache misses do not introduce a latency tail.

Rev1 is therefore defined as a **controlled, isolated Cortex-M33 polling baseline without PIO bus-phase ownership**, not as an application-level USB-loaded benchmark.

### Rev1 P1 — T1 capture correctness

Every cycle, including cycle zero, must establish the same initial condition:

```text
wait ALE low
wait ALE high
track GPIO continuously while ALE remains high
retain the final coherent ALE-high snapshot
```

Recommended software-transparent-latch rule:

```c
uint32_t t1 = 0u;
uint32_t s;
uint32_t n = 0u;

wait ALE low;
wait ALE high -> s;

do {
    t1 = s;
    ++n;
    s = sio_hw->gpio_in;
} while (ALE remains high in s);
```

All relevant pins are within GPIO 0–27, so one `sio_hw->gpio_in` read is a coherent 32-bit snapshot.

The `first_cycle` special case must be removed.

The control/write-data sample must be **re-derived from the corrected T1 anchor**. Rev0 does not prove the existing control phase correct.

Cycle zero is a hard protocol assertion:

```text
address = FFFF0
cycle   = MEM_READ
lanes   = WORD
```

Any mismatch is an immediate capture failure and aborts the point.

### Direct polling-margin metric: `n`

For each cycle record the number of coherent ALE-high samples captured by the polling loop:

```text
n = ALE-high coherent sample count
```

Report at least:

```text
min(n) for the frequency point
```

When `min(n)` approaches 1, software polling has effectively lost address-capture margin even if a particular workload still happens to pass. This is a direct characterization metric, not a post-failure inference.

For the first 0.300 MHz gate require:

```text
min(n) >= 3
```

### Rev1 P2 — observability

Maintain a bounded ring buffer without per-cycle USB output. Each entry should contain at least:

```text
clock count since RESET release
raw T1 GPIO snapshot
raw A19:A16 nibble
ALE-high sample count n
raw control/data-phase snapshot
decoded address
cycle type
lane state
response latency when applicable
```

Also retain:

- explicit first-anomalous-cycle evidence;
- independent ALE pulse count;
- serviced-cycle count;
- internally measured average V30 clock;
- worst-case control/T2-to-AD-data-driven latency;
- AD-release-to-next-ALE margin.

Any difference between independent ALE pulse count and serviced-cycle count invalidates the point.

### Clock reporting

Rev1 should log both:

```text
Configured V30 clock
Internally measured average V30 clock
```

A hardware edge counter such as an available PWM B-channel edge-count path or equivalent deterministic counter may be used to measure actual CLK edges over a reference window.

Internal edge counting validates average frequency and catches configuration errors. External scope validation remains the reference for duty cycle, fractional-divider jitter, rise/fall behavior, ringing and electrical margin.

Fractional PIO divider settings should be logged where used.

### Read-response latency

Measure the worst-case interval from the accepted control/T2 event to the instruction or hardware action that actually drives response data onto AD.

Report at least:

```text
worst-case RP2350 cycles
worst-case ns
worst-case latency as a fraction of the available V30 response window
```

This instrumentation must already exist in Rev1 so that a later PC1-B comparison can attribute any improvement.

---

## PC1-A Rev1 first gate — 0.300 MHz only

Do **not** begin Rev1 with a sweep.

The first hardware gate is a single controlled point at 0.300 MHz.

Acceptance criteria:

```text
RESET held for the defined N clocks
RESET released on the defined phase
cycle 0 = FFFF0 / MEM_READ / WORD
expected FFFFx -> F000x boot/fetch progression
min(ALE-high samples n) >= 3
independent ALE pulse count == serviced-cycle count
no first-fault capture anomaly
PIC ICW1-4 / IMR programming
PIT OUT 43h / 40h programming
PIT terminal count
IRQ0 through pi86_pic
INTR
exactly two INTA cycles
vector 20h
IVT
ISR / marker
EOI
IRET
final IRR=00h / ISR=00h / INTR=0
SUCCESS 3/3
```

If this gate fails, dump the bounded ring buffer and debug only the first fault.

If P0–P2 are complete and the controlled 0.300 MHz gate still cannot reach a clean PASS within a bounded debugging effort, **timebox PC1-A Rev1 and proceed to PC1-B** rather than continuing to rescue the polling architecture.

---

## PC1-A Rev1 controlled sweep

Only after the 0.300 MHz gate passes, run the comparable 13-point sweep:

```text
0.100
0.200
0.300
0.500
0.750
1.000
2.000
2.500
3.000
4.000
4.770
6.000
8.000 MHz
```

Every point must use identical:

- RESET clock count;
- RESET release phase;
- T1 and control sampling rules;
- timeout meaning in V30 clocks;
- interrupt/runtime isolation;
- SRAM placement policy;
- workload;
- PASS criteria.

Per-point output should include at least:

```text
Configured average clock
Internally measured average clock
PIO divider
min(ALE-high samples n)
independent ALE count
serviced cycle count
worst response latency
AD-release margin
PASS / FAIL
first-fault category
```

The valid PC1-A deliverable is:

```text
last known-good controlled polling frequency
first failing controlled polling frequency
capture-margin trend via min(n)
first measured binding constraint
```

---

## PC1-B — PIO-timed bus engine

PC1-B remains the preferred target architecture for deterministic phase ownership and higher-frequency timing margin, but **PC1-A Rev0 is not evidence for that architectural claim**. PC1-B must be compared against the valid PC1-A Rev1 control group when Rev1 is available.

Minimum responsibility split:

```text
PIO clock/reset state machine
  -> deterministic CLK
  -> RESET in integer V30 clocks
  -> deterministic RESET release phase

PIO bus-capture state machine
  -> deterministic T-state ownership
  -> ALE/T1 capture
  -> phase-aligned control/data capture
  -> clock/phase metadata
  -> deterministic event handoff

Cortex-M33
  -> event decode
  -> memory / I/O / PIC / PIT semantics
  -> read/vector response initially through direct SIO
  -> backend policy
```

### PIO state-machine synchronization

Phase-coupled state machines must be fully initialized while disabled and started in sync using:

```c
pio_enable_sm_mask_in_sync(...)
```

or an equivalent mechanism that both enables the selected SMs together and restarts/aligns their clock dividers. A simple pair of independent enable operations is insufficient.

### PC1-B first gate

PC1-B must also start at one 0.300 MHz end-to-end functional point before any sweep.

Use the same semantic Gate 12 path and equivalent observability so PC1-B can be compared directly to PC1-A Rev1.

### PC1-B optimization order

Do not add DMA by default.

Use this order:

1. deterministic PIO clock/reset sequencing;
2. synchronized PIO SM startup;
3. PIO ownership of T1/control phase capture;
4. preserve ring-buffer and clock/latency instrumentation;
5. keep data response on Cortex-M33/SIO initially for attribution;
6. optimize direct SIO hot path if measured response latency requires it;
7. optimize SRAM placement / dispatch further if required;
8. move more response timing into PIO only when the measured deadline requires it;
9. use DMA only if a measured problem is actually improved by DMA;
10. rerun comparable characterization after each architectural change.

The primary comparison remains:

```text
original Pi86 historical baseline  ~0.3 MHz
PC1-A Rev1 controlled polling      measured by this project
primary target                     4.77 MHz
stretch target                     8 MHz class
```

---

## Logging rule

No per-bus-cycle USB logging during timing-critical execution.

Dump evidence only after a point has stopped.

For valid Rev1/PC1-B measurements, distinguish:

```text
configured average clock
internally measured average clock
external waveform-quality validation
```

A reset-vector observation alone is never a PASS.

---

## Final status of this characterization record

```text
Gate 12 physical functional baseline   PASS
PC1-A Rev0                            INVALID characterization; defect evidence retained
PC1-A Rev1                            CONTROL PLAN RETAINED; NOT THE SELECTED ARCHITECTURE
PC1-B PIO-direct fixed response       PASS, 0.300-8.000 MHz configured clock
PC1-C address-qualified/bounded work  CONTINUED IN SEPARATE PLANS AND VALIDATION
```

## Milestone completion

The fixed-response portion of Performance Characterization 1 is complete. Its result selects DMA-fed PIO-direct GPIO ownership as the deterministic response architecture.

The broader questions moved into PC1-C and later companion-chip work:

1. capture and qualify the V30 address/control cycle;
2. select the correct SRAM-backed ROM bytes and byte lanes;
3. meet the response deadline without relying on V30 wait states, because the current HAT ties `READY` high;
4. validate CPU-visible execution before sweeping frequency; and
5. report address-qualified performance separately from the PC1-B fixed-response ceiling.

Consult current architecture and validation documents for the accepted answer to each boundary; do not infer integrated-system frequency from this historical fixed-response record.
