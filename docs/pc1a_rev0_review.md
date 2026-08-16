# PC1-A Rev0 — Engineering Review

Scope: review of the PC1-A continuous software-polling harness, the proposed T1 root cause, and the proposed correction plan, prior to PC1-B.

Reviewed tree: `8412e1c`. Source under `tests/performance_characterization_1/` is byte-identical to `21d1911`; only `docs/performance_characterization_1.md` differs. All source observations below therefore apply to the tree as it stands.

Verdict summary:

```text
T1/address root cause                  CORRECT, with one caveat on the evidence
{0,1,2,3} fingerprint argument         CORRECT conclusion, INVALID statistic as stated
RESET wall-clock delay                 CORRECT, is an experiment-control defect
RESET gpio_init transient              CORRECT, worth fixing, not causal
"control phase is already correct"     NOT ESTABLISHED - see Finding C
PC1-A Rev0 = INVALID                   AGREE
0.300 MHz single-point gate            AGREE
Proposed Rev1 controls sufficient?     NO - five uncontrolled variables remain
Recommendation                         A, with an expanded Rev1 scope
```

---

## Part 1 — What the review confirms

### The T1 sampling analysis is technically correct

`capture_cycle()` takes the T1 word from the first polling iteration in which ALE reads high. The known-good stepped engine takes it after a complete HIGH→LOW pulse. The two sample opposite ends of T1. That much is not in dispute and is visible in the source.

The timing argument behind it also holds. On the 8086/V20/V30 external bus, address and ALE are both emitted relative to the CLK edge that begins T1, through *independent* propagation paths:

```text
T1 start (CLK edge)
   ├── T_CLAV   clock to address valid
   └── T_CLLH   clock to ALE high
```

`T_CLAV` is the larger of the two. ALE can therefore be observed high before the address has propagated. The guarantee the part actually gives is `T_AVAL` — address valid **to ALE low** — which is why the conventional 8282/8283 latch is transparent while ALE is high and captures on the falling edge.

Sampling at first-ALE-high has no validity guarantee behind it. This is a **protocol/timing bug**, not merely an implementation detail: the harness is reading the bus at a phase the device does not warrant.

### The RESET wall-clock delay is a genuine experiment-control defect

Confirmed at `performance_characterization_1.c`:

```c
hold_reset(true);
perf_clock_start(clock, frequency_hz);
busy_wait_us_32(RESET_SETTLE_US);   /* 50 us, fixed */
hold_reset(false);
```

against the stepped engine's clock-counted form in `v30_bus_reset_sequence()`. 5 V30 clocks at 0.100 MHz versus 400 at 8.000 MHz.

This is correctly classified as a **methodology defect** rather than a coding defect. The consequence is not that any single point is wrong — it is that the 13 points are not samples of one experiment. Frequency was not the only variable. No comparison across the sweep is admissible, including the "fails even at 0.100 MHz" argument, which is the load-bearing claim of the whole run.

### The RESET GPIO transient is worth fixing

`gpio_init()` sets the pin to input; `gpio_disable_pulls()` then removes any pull; the level is driven only afterwards. RESET is briefly undriven and unpulled on every transition. Under a host-stepped clock the CPU is frozen during that window. Under a free-running clock it is not.

Classify this as a **correctness fix, not causal**. It should be fixed because an undriven asynchronous reset input on a running CPU is indefensible, not because it explains the observed failures. Do not expect the failure pattern to change when it is fixed.

---

## Part 2 — Corrections to the analysis

### Finding A — the `{0,1,2,3}` statistic is invalid as stated; the conclusion survives

The claim "nine of ten failure addresses have upper nibble in `{0,1,2,3}`, never `4h`–`Fh`" treats the observed failures as an unbiased sample. They are not. The memory map is:

```text
RAM   0x00000 - 0x0FFFF      upper nibble 0    mapped
ROM   0xF0000 - 0xFFFFF      upper nibble F    mapped
everything else              nibbles 1..E      unmapped -> "memory transaction failure"
```

A corrupted upper nibble is only *observable* if it lands outside the map. So:

- a corruption to nibble `0` aliases into RAM and is **serviced silently with wrong data**;
- a corruption to nibble `F` aliases into ROM and is **serviced silently with wrong data**;
- only nibbles `1`–`E` ever surface as a recorded failure.

The statement "never `4h`–`Fh`" is therefore uninformative — `F` corruptions cannot appear in a failure list by construction. Likewise the absence of `0` is an artifact, not evidence.

**Restate the argument in the form that does survive.** The observable failure space is 14 nibbles (`1`–`E`). Nine of ten observed failures fall in the three nibbles `{1,2,3}` that the status hypothesis predicts, in a ratio consistent with the workload's segment usage:

```text
2xxxx   x6    S4:S3 = 10   CS    instruction fetch   dominant, as expected
1xxxx   x2    S4:S3 = 01   SS    stack
3xxxx   x1    S4:S3 = 11   DS    data
                            ES -> nibble 0, aliases into RAM, unobservable
```

Nine of ten in three of fourteen bins is not a chance distribution. The conclusion holds. The statistic backing it must be rewritten before it goes in the document, because as currently phrased a reviewer can dismiss it.

### Finding B — the strongest evidence is not the distribution, it is two individual captures

Two single data points carry more weight than the histogram.

**`AFFF4` at 0.500 MHz.** The reset-vector region is `FFFF0`/`FFFF2`/`FFFF4`. `AFFF4` is `FFFF4` with the upper nibble reading `A` = `1010` instead of `F` = `1111`. Under the status hypothesis the nibble was in transit from `F` (address) toward `2` (CS status) — or the reverse — and was captured with bits 18 and 16 already flipped and bits 19 and 17 not yet. `A` is not a valid status value (it requires S6 = 1, which the part does not produce). This is strongly consistent with a mid-transition capture.

**`2FFF0`, type = MEM_WRITE, at 4.000 MHz, with `Serviced bus cycles = 0/360`.** This is the *first* bus cycle after RESET release. It must be the reset-vector fetch: `FFFF0`, MEM_READ, WORD. It was captured as `2FFF0`, MEM_WRITE. No instruction has executed, so no CPU derailment can be involved. This is pure capture corruption, on cycle zero, and it is the cleanest single piece of evidence in the entire run.

Note what else it shows — see Finding C.

### Finding C — the control/write-data phase is NOT established as correct

The proposal states the control sample is already phase-aligned with the proven stepped engine and should be retained unchanged. The `2FFF0` capture contradicts that.

DT/R̄ was read as transmit on a cycle that can only have been a read, on cycle zero, before any instruction executed. Direction is decoded from the control sample:

```c
cycle->dtr = (uint8_t)sample_bit(control, V30_PIN_DTR);
...
return dtr == 0u ? V30_BUS_CYCLE_MEM_READ : V30_BUS_CYCLE_MEM_WRITE;
```

So the control sample was also wrong on that cycle. Either the control phase is misaligned in some condition the comment did not anticipate, or the whole cycle was captured one phase off and both samples inherited the error.

The likely mechanism is the `first_cycle` path:

```c
if (!first_cycle) {
    if (!wait_signal_level(V30_PIN_ALE, false, NULL)) { ... }
}
if (!wait_signal_level(V30_PIN_ALE, true, &t1)) { ... }
```

On the first cycle the harness does not first establish that ALE is low. It begins polling immediately after `hold_reset(false)` and accepts the first sample in which ALE reads high. If ALE is high or glitching at that moment — during the CPU's internal reset synchronization, or from the RESET pin's own undriven transition window — the harness locks onto a phantom T1 and every subsequent sample in that cycle is offset.

**Action:** do not carry the control phase forward on the assumption that it is proven. Re-derive it in Rev1 from the corrected T1 anchor and assert it explicitly on cycle zero (`address == FFFF0 && type == MEM_READ && lanes == WORD`). Treat a cycle-zero mismatch as an immediate abort, not a logged failure.

### Finding D — cycle-zero captures are structurally the most fragile, and five of thirteen points died there

Points 0.100, 0.500, 2.000, 2.500 and 4.000 MHz all report `Serviced bus cycles = 0/360`. They failed on the first cycle.

This is expected once the bus's initial condition is considered. During RESET the V30 three-states AD; the harness has also called `release_ad()`; the pins have pulls disabled. **Before the first T1 the AD bus is floating**, not holding a previous value. A sample taken before the CPU's address drivers turn on therefore reads an arbitrary floating value, not a stale-but-structured one.

That accounts for `20164` at 0.100 MHz, where the low 16 bits (`0164`) bear no relation to `FFF0` — unlike the later failures, where the low 16 bits are largely intact and only the nibble is wrong. Two different corruption signatures, one mechanism, distinguished by whether the bus was floating or driven before the sample.

This matters for Rev1 acceptance: **the reset-vector fetch is the hardest cycle in the run, not the easiest.** "Correct `FFFF0` reset fetch" is a strong gate, and the right one.

### Finding E — the recorded failure address is not necessarily the first corrupted cycle

A corruption to nibble `0` or `F` is serviced silently from RAM or ROM. RAM is initialised to `0x00` except the IVT entry, so a mis-aliased code fetch returns `0x0000` = `ADD [BX+SI],AL`, which the V30 executes.

Consequently, for any point that serviced more than zero cycles, the reported failure address may be a *genuine* address generated by an already-derailed CPU rather than a mis-sampled one. The 1.000 MHz `2FFFC` MEM_WRITE (after 2 serviced cycles) is ambiguous in exactly this way — the workload's first legitimate write is a stack push far later in the program.

This also gives a better account of the three ALE-timeout points (0.200, 0.300, 0.750 MHz) than "host missed bus timing": a garbage instruction stream will eventually decode `HLT` (`F4h`) or `WAIT` (`9Bh`), after which the CPU stops generating bus cycles and ALE never returns. That is consistent with those three points having read the reset vector correctly and then died a few cycles later.

**The ring buffer is therefore not an optional diagnostic aid. Without it, PC1-A cannot distinguish a capture fault from a derailment downstream of one, and no failure address in Rev0 can be interpreted as a first fault.**

---

## Part 3 — Confounders not yet identified

These are in addition to the three defects already named. Items 1–3 must be closed before Rev1 is run or the sweep will again fail to isolate frequency.

### 1. `MAX_SIGNAL_SPINS` is a fixed iteration count — the same defect class as `RESET_SETTLE_US`

```c
#define MAX_SIGNAL_SPINS 200000u
```

`wait_signal_level()` spins a fixed number of *loop iterations*, i.e. a fixed wall-clock interval independent of V30 clock. Expressed in V30 clocks, the timeout threshold varies substantially across the sweep. "ALE timeout" therefore does not mean the same thing at different frequency points. Normalize timeout budgets to V30 clocks.

### 2. Interrupts are never masked, and USB stdio is active during every point

A software-polling bus engine with interrupts enabled and a USB device stack running is not a controlled environment. This does not explain the Rev0 cycle-zero failures, but it can corrupt a longer Rev1 run that gets past the early cycles.

Minimum: mask interrupts for the duration of a frequency point and perform all `printf` between points only. A stronger split can place the timing-critical bus loop on one core with interrupts masked while another core owns USB, if needed later.

### 3. XIP execution makes service latency nondeterministic

The timing-critical polling/service path is not forced into SRAM. XIP cache misses create a latency tail. For a controlled polling baseline, critical code/data should be SRAM-resident before using pass/fail boundaries as an architectural measurement.

### 4. PIO clock divider is fractional at several sweep points

The PIO fractional divider introduces bounded period jitter and possible duty-cycle asymmetry at non-integer divider values. This is not considered causal for Rev0, but it means signal quality is not identical across all points. Record achieved divider / expected average frequency and characterize waveform quality separately.

### 5. AD release timing leaves no guarded margin against the next T1

`release_ad()` occurs near the next-address boundary and is itself reached through software polling latency. No contention has been proven, but Rev1 should measure release-to-next-ALE margin rather than assume it.

### 6. There is no missed-cycle detector

If an ALE pulse is missed entirely, the harness can service the next cycle as though nothing happened. An independent ALE pulse counter should be compared against the serviced-cycle count. Any divergence invalidates the point.

---

## Part 4 — Recommended Rev1 sampling rule

Use a **software transparent latch**: retain the most recent coherent sample in which ALE still reads high.

```c
uint32_t t1 = 0u;
uint32_t s;
uint32_t n = 0u;

if (!wait_signal_level(V30_PIN_ALE, false, NULL)) return ALE_TIMEOUT;
if (!wait_signal_level(V30_PIN_ALE, true, &s)) return ALE_TIMEOUT;

do {
    t1 = s;
    ++n;
    s = sio_hw->gpio_in;
} while (sample_bit(s, V30_PIN_ALE));
```

Why this rule:

- `sio_hw->gpio_in` is a single 32-bit read and all relevant V30 pins are within GPIO 0–27, so each sample is coherent.
- It approximates transparent-latch behavior while ALE is high and retains the final ALE-high value.
- `n` becomes a direct polling capture-margin measurement.

Record `min(n)` per frequency point. When `min(n)` approaches 1, the polling architecture has effectively lost capture margin regardless of whether the workload still happens to pass.

The control sample must be re-derived from this corrected T1 anchor rather than assumed correct from Rev0.

---

## Part 5 — Minimum corrective patch plan

```text
P0  experiment control
    1. mask interrupts for the duration of a frequency point;
       all printf between points only
    2. RESET hold expressed in V30 clocks, identical N at every point,
       release at a defined CLK edge
    3. RESET GPIO initialised once; direct set/clr on transitions
    4. ALE/CLK timeouts expressed in V30 clocks, not loop iterations
    5. service path resident in SRAM (__not_in_flash_func or equivalent placement)

P1  capture correctness
    6. T1/address = last coherent sample with ALE high; count n
    7. unconditional ALE-low establishment, including cycle zero;
       delete the first_cycle special case
    8. re-derive the control sample from the corrected T1 anchor
    9. cycle-zero assertion: FFFF0 / MEM_READ / WORD, hard abort on mismatch

P2  observability
   10. ring buffer with raw T1/control snapshots, decoded fields, clock count,
       ALE-high sample count n, and first-fault evidence
   11. independent ALE pulse counter; compare against serviced count
   12. internal clock self-measurement; log configured vs measured average frequency
   13. response-latency measurement, worst case
   14. AD-release margin measurement

P3  measurement
   15. single point at 0.300 MHz against the full Gate 12 acceptance list,
       plus min(n) >= 3 and zero missed cycles
   16. only then the 13-point sweep, with per-point min(n) reported

P4  optional, do not block Rev1
   17. integer-only clock dividers, or per-point jitter recorded
   18. external scope waveform-quality checks where needed
```

---

## Part 6 — PC1-B guidance retained after Rev1

PC1-B remains the preferred architecture direction for deterministic phase ownership and higher-frequency timing margin, but PC1-A Rev0 must not be used as proof that software polling itself is insufficient.

PC1-B rules retained:

- initialise phase-coupled PIO SMs disabled and start them with `pio_enable_sm_mask_in_sync()` or equivalent divider-restarted synchronous enable;
- express RESET duration in V30 clocks and release on a deterministic phase;
- start with a 0.300 MHz single-point functional gate, not a sweep;
- retain a failure ring buffer;
- add internal clock self-measurement and external waveform-quality validation as separate concerns;
- instrument control/T2-to-AD-data-driven worst-case latency;
- initially keep data response on Cortex-M33/SIO for attribution;
- do not add DMA without measured evidence.

---

## Final recommendation

**A — fix PC1-A and rerun it before PC1-B**, with the expanded Rev1 scope above.

PC1-B without a valid PC1-A has no clean control group. Rev1 should be timeboxed: if P0–P2 are complete and the 0.300 MHz full-function gate still does not pass cleanly, stop iterating on the polling harness and escalate to PC1-B.

The purpose of Rev1 is to produce a credible baseline, not to rescue polling as the final architecture.
