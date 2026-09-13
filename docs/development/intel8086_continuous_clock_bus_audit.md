# Intel 8086 Continuous-Clock Bus Architecture Audit

## Purpose

This document records the architecture audit required by issue #102 after the
FreeRTOS #75 software-execution and RP2350 transaction-model investigations
converged on the physical processor clock/bus contract.

It is intentionally an architecture decision input, not a claim that the clock
contract is already the demonstrated root cause of #75.

The current Intel P8086-2 investigation contract requires a continuously valid
processor clock at or above the documented minimum operating frequency. The
existing Pi86 HAT also fixes `READY` asserted and does not expose it to the
RP2350, so firmware cannot extend arbitrary bus cycles with Intel wait states on
this hardware revision.

The central question is therefore:

> Can the existing RP2350 realtime machinery serve arbitrary `.P86W` workloads
> while the physical Intel 8086 clock runs continuously, or is a new dynamic
> realtime bus engine required?

---

## 1. Established Hardware Constraints

### 1.1 READY is not available to firmware

The locked HAT routing records processor pin 22 `READY` tied directly to 3.3 V.
It is not part of the RP2350 GPIO mapping.

Therefore the present HAT cannot implement the conventional arrangement:

```text
continuous Intel CLK
        +
RP2350-controlled READY wait states
```

without a hardware modification.

This is a hard boundary for the existing no-redesign HAT.

### 1.2 CLOCK_STEPPED intentionally stops the processor clock

`firmware/runtime/clock_stepped_clock.pio` implements:

```pio
pull block
set pins, 1 [9]
set pins, 0 [9]
mov isr, null
push block
```

At the configured 2 MHz PIO state-machine rate, the high and low phases are
5 us each, for a nominal 100 kHz processor clock while steps are being issued.
More importantly, `pull block` deliberately parks CLK low for an unbounded
firmware-controlled interval between steps.

That mechanism is useful for deterministic bring-up and transaction inspection,
but it is not an Intel-compliant general execution clock for the P8086-2
contract being audited in #102.

### 1.3 The existing free-running generator is continuous, but its canonical
frequency is not yet an Intel-compliant solution

`execution_clock_control.pio` generates a continuous clock and checks a stop
token only after a complete low half-cycle. `execution_clock_controller.c`
configures this state machine at ten PIO cycles per processor clock.

The canonical firmware build currently defines:

```text
RP86_PROCESSOR_HZ = 1,000,000
```

Therefore the existing canonical free-running runtime demonstrates the correct
**ownership pattern**—PIO owns the processor clock continuously—but its current
1 MHz configuration is still below the 2 MHz Intel minimum assumed by issue
#102. Merely changing this constant is not sufficient, because every current
bus-cycle response must also meet the shorter bus timing at the higher clock.

---

## 2. What the Current CLOCK_STEPPED Path Actually Buys Us

The general `.P86W` workload path currently uses:

```text
workload_executor.c
        ↓
rp86_clock_stepped_service_cycle()
        ↓
processor_bus_wait_cycle()
        ↓
M33 classifies the transaction
        ↓
C memory / I/O dispatch
        ↓
M33 prepares data or records write
        ↓
processor_bus_complete_*
        ↓
next processor clock steps
```

The important property is not simply that the clock is slow. The implementation
**uses stopped-clock time as its wait-state mechanism**.

For example, a memory read currently performs all of the following between
processor clock steps:

1. wait for and sample T1;
2. decode the 20-bit address and byte lanes in C;
3. sample the control phase;
4. classify memory versus I/O versus INTA;
5. call the C memory mapper;
6. obtain the requested data;
7. encode scattered AD GPIO output bits;
8. enable the requested AD lanes;
9. issue subsequent processor clock steps.

At continuous >=2 MHz operation with READY permanently asserted, this M33
round-trip cannot be assumed to fit inside the active bus cycle. The present
clock-stepped controller therefore cannot simply be switched to free-running
clock mode while retaining the same per-cycle service algorithm.

---

## 3. Existing Realtime PIO Machinery

The repository already contains two important realtime mechanisms that prove
parts of the required design are possible without M33 participation in the
current bus cycle.

### 3.1 Prepared exact-response engine

`prepared_bus_responder.pio` splits responsibility into:

```text
early-T1 matcher
        ↓
exact raw GPIO key
        ↓
paired response descriptor
        ↓
PIO-owned AD value + output-enable timing
```

Relevant properties:

- synchronized early-T1 sampling is performed by PIO;
- valid read data is prepared and enabled at an explicit bus phase;
- AD is released at an explicit H2 boundary;
- unrelated cycles remain high-Z;
- an unsupported cycle does not consume the current descriptor.

This is an excellent timing primitive.

It is deliberately a **bounded known-path engine**, however. Its response data
is paired in advance with an exact expected physical bus key.

### 3.2 Persistent processor-service responder

`processor_service.pio` extends the same idea into the persistent companion
runtime:

```text
prepared exact response streams
        +
PIO-owned two-cycle INTA responder
        +
DMA-fed descriptors
```

Again, PIO owns the timing-critical current-cycle behavior while M33 owns policy
outside the current cycle.

This validates the architectural rule:

> No M33 answer belongs inside a no-wait-state processor bus cycle.

But the current service responder still consumes an ordered sequence of exact
keys and prepared responses. It is not a random-access memory engine.

---

## 4. Capability Matrix

| Requirement | CLOCK_STEPPED C path | Prepared / service PIO | Needed for general continuous `.P86W` |
|---|---|---|---|
| Continuous processor clock | No | Yes | Yes |
| >=2 MHz Intel clock target | No | Mechanism supports configurable free-running rate; current canonical setting is 1 MHz | Yes |
| Early-T1 hardware sampling | By stepped M33 sampling | Yes | Yes |
| Exact known read response | Yes | Yes | Yes |
| Arbitrary memory address read | Yes, because M33 has unlimited stopped-clock time | No general lookup | Yes |
| Arbitrary memory write capture | Yes | Exact/prepared path only | Yes |
| Dynamic I/O read dispatch | Yes | Prepared values only | Yes |
| Dynamic I/O write dispatch | Yes | Prepared/observed cases only | Yes |
| Two-cycle INTA timing | Yes | Yes | Yes |
| READY wait-state insertion | Not used; clock itself stalls | Not available on HAT | Not available |
| Current-cycle dependency on M33 | Yes | No for prepared cycles | Must be No |

The important conclusion is:

> The existing prepared machinery is reusable as a **timing architecture**, but
> it is not directly generalizable by configuration alone into an arbitrary
> workload memory system.

The missing capability is a hardware-paced **dynamic address → response** path.

---

## 5. Why a Larger Prepared Trace Is Not the General Solution

It would be tempting to precompute a much larger descriptor stream for a
`.P86W` workload and let the current exact matcher replay it at continuous
clock speed.

That can be useful for deterministic validation traces, but it cannot be the
general runtime because normal software contains runtime-dependent traffic:

- stack addresses depend on control flow;
- queue/list contents change dynamically;
- memory reads depend on earlier writes;
- interrupt timing changes instruction flow;
- device reads may depend on live state;
- self-modifying or generated data changes subsequent transactions;
- arbitrary user workloads are not known as one fixed bus transcript.

A prepared transcript is therefore evidence/replay infrastructure, not a
replacement for a dynamic memory/I/O service plane.

---

## 6. Required Architecture for Intel-Compliant General Execution

For the existing HAT, the general solution has to satisfy all of these at once:

```text
CLK continuously >= Intel minimum
READY permanently asserted
arbitrary 20-bit memory address
arbitrary memory writes
processor I/O
INTA
no M33 current-cycle dependency
```

That implies a new realtime data path whose current-cycle ownership stays in
PIO/DMA/hardware-paced state.

The architectural target is:

```text
                 physical Intel 8086
                         │
                         │ continuous CLK
                         ▼
              ┌──────────────────────┐
              │ realtime PIO front   │
              │ end                  │
              │                      │
              │ - T1 capture         │
              │ - cycle classify     │
              │ - AD direction       │
              │ - read drive timing  │
              │ - write capture      │
              │ - INTA timing        │
              └──────────┬───────────┘
                         │ hardware-paced
                         ▼
              ┌──────────────────────┐
              │ deterministic data   │
              │ plane                │
              │                      │
              │ SRAM / DMA / tables  │
              └──────────┬───────────┘
                         │ committed transactions
                         ▼
              ┌──────────────────────┐
              │ M33 policy plane     │
              │                      │
              │ device policy        │
              │ host service         │
              │ telemetry            │
              │ refill / maintenance │
              └──────────────────────┘
```

The current prepared responder can contribute the PIO timing and pin-ownership
rules. The clock-stepped memory mapper can contribute memory semantics. Neither
one, by itself, supplies the missing dynamic realtime lookup path.

---

## 7. Candidate Dynamic Data-Plane Strategies

These are architecture candidates, not yet demonstrated implementations.

### Candidate A — PIO + DMA address-driven SRAM service

Concept:

```text
PIO captures transaction/address
        ↓ DREQ
DMA/control path derives or installs SRAM source/destination
        ↓
PIO receives read word or commits write word
        ↓
PIO performs bus-phase drive/release
```

Advantages:

- retains PIO ownership of active bus timing;
- internal SRAM can hold deterministic processor-visible memory;
- M33 can remain outside current-cycle latency;
- RP2350 DMA is explicitly designed for PIO DREQ pacing.

Unproven point:

> The project has not yet demonstrated a DMA control topology capable of using
> an arbitrary captured 20-bit processor address as a sufficiently fast dynamic
> SRAM lookup/write destination within the no-wait-state 8086 bus budget.

This requires a focused RP2350 DMA/control-block feasibility experiment before
it is accepted as the architecture.

### Candidate B — PIO-managed hot windows plus prepared descriptors

Use deterministic PIO/prepared responses for selected fixed/hot regions and
fall back to another mechanism for dynamic regions.

This can accelerate ROM, vectors, fixed service stubs, and tightly bounded
mailboxes, but it does not solve arbitrary RAM by itself.

### Candidate C — new dedicated continuous-clock general bus engine

Build a separate PIO/DMA engine specifically around random-access processor
memory and I/O, reusing proven electrical/timing primitives from the prepared
runtime but not its ordered exact-descriptor abstraction.

This is the cleanest conceptual solution if Candidate A proves practical, but
it is also the largest firmware change.

### Candidate D — hardware revision exposing READY

A future HAT could route READY to an RP2350-controlled signal, enabling a much
more conventional architecture:

```text
continuous >=2 MHz CLK
        +
READY-generated wait states
        +
M33 / memory service during Tw
```

This is explicitly outside the current no-HAT-redesign constraint.

---

## 8. Internal SRAM Must Remain the Baseline

A continuous-clock bus solution must not depend on optional external PSRAM.

The existing project policy already treats internal SRAM as the deterministic
fast path. The minimum workload tier is therefore still backed by internal SRAM;
PSRAM, when present, is an optional capacity extension.

This matters for the realtime engine because an accepted design must first work
for the currently available hardware configuration:

```text
Internal SRAM  available
External PSRAM optional / may be absent
```

The realtime memory interface should consequently expose a backing abstraction
that can later attach PSRAM without changing the processor-facing bus engine.

---

## 9. Clock Target Correction

The repository currently has two distinct clock concepts:

```text
CLOCK_STEPPED     nominal ~100 kHz while stepping, arbitrary low stalls
FREE_RUNNING      canonical build currently 1 MHz
```

For Intel P8086-2 general execution under the #102 contract, neither current
setting is sufficient.

The required future state is conceptually:

```text
Intel general execution:
    FREE_RUNNING
    >= 2 MHz
    no current-cycle M33 dependency

Bring-up / diagnostic stepping:
    CLOCK_STEPPED
    explicitly non-compliant / diagnostic-only for Intel
```

This separation should eventually be represented explicitly in capability and
runtime policy instead of allowing `CLOCK_STEPPED` to look like an equivalent
general-execution mode for Intel.

No constant should be changed until the dynamic bus-service path is proven.

---

## 10. Decision from This Audit

### Reusable without architectural change

- free-running PIO clock generator;
- exact early-T1 sampling concepts;
- scattered AD bitmap packing;
- PIO-owned AD output-enable timing;
- H2 release timing;
- PIO/DMA stream feeding;
- physical INTA responder concepts;
- M33-as-policy-plane ownership rule;
- existing memory-map semantics.

### Not sufficient for general Intel execution

- `clock_stepped_bus_controller.c` as the active-cycle service mechanism;
- ordered exact-response streams as arbitrary RAM;
- M33 address decode and memory lookup between CPU clock steps;
- current 1 MHz canonical free-running clock setting;
- any design that assumes READY can be asserted by firmware.

### Architectural conclusion

> **The current HAT requires a new dynamic continuous-clock realtime bus data
> plane for Intel-compliant arbitrary `.P86W` execution.**
>
> Existing prepared PIO/DMA machinery should be reused as timing/electrical
> building blocks, but should not be stretched into a false general-memory
> abstraction.

This conclusion does not yet prove that such a dynamic PIO/DMA engine can meet
all timing requirements on RP2350. That is the next feasibility gate.

---

## 11. Next Feasibility Gate

Before modifying production runtime behavior, build a narrowly scoped
engineering experiment that answers one question:

> Can RP2350 PIO + DMA, with no M33 participation inside the bus cycle, service
> arbitrary internal-SRAM-backed 8086 word/byte reads and writes at a continuous
> >=2 MHz processor clock on the existing fixed-READY HAT?

Minimum experiment requirements:

1. free-running processor clock at an Intel-compliant test rate;
2. PIO capture of 20-bit address, lane state, and read/write type;
3. random internal-SRAM-backed read response;
4. random internal-SRAM-backed write capture;
5. no per-cycle M33 callback;
6. exact AD drive/release timing evidence;
7. byte-lane correctness for low, high, and word transactions;
8. bounded failure behavior for unsupported I/O;
9. bus trace sufficient to compare requested address/data with SRAM contents;
10. implementation isolated from the production runtime until the timing
    contract is demonstrated.

If this experiment passes, the result can become the foundation of the general
continuous-clock workload engine. If it cannot be made deterministic within the
8086 bus budget, the existing HAT cannot support Intel-compliant arbitrary
workloads without either a different hardware mechanism or exposing READY.

---

## 12. Relationship to FreeRTOS #75

The evidence chain entering #102 is now:

```text
real FreeRTOS delayed-list machine code           PASS
real xQueueReceive caller path                    PASS
real initial task restore                         PASS
controlled real tick interleaving                 PASS
executor + clock-stepped controller composition  PASS
                                                   │
                                                   ▼
physical clock contract remains out of spec
```

This makes the clock/bus contract the highest-value remaining boundary.

It still does **not** justify the statement:

```text
CLOCK_STEPPED is proven root cause of #75
```

The correct statement is:

> The current Intel CLOCK_STEPPED execution mode violates the documented
> processor operating clock contract, and all investigated software layers
> above that physical boundary have remained coherent. An Intel-compliant
> continuous-clock bus path must therefore be established before #75 can be
> meaningfully re-evaluated on physical silicon.
