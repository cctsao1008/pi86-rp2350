# Intel 8086 Clock Contract on the Current Pi86 HAT

## Status

Architecture audit for Issue #102.

This document defines the Intel 8086 clocking boundary for the current, unmodified Pi86 HAT. It records what the existing hardware can and cannot claim as a vendor-compliant execution contract. It does not change firmware behavior or invalidate historical physical measurements.

## Executive conclusion

The current Intel 8086 execution architecture has a hard hardware boundary:

- the Pi86 HAT ties processor `READY` asserted;
- the RP2350 therefore cannot insert ordinary Intel 8086 wait states;
- the current `CLOCK_STEPPED` controller intentionally holds `CLK` low between software-issued pulses;
- the current stepped waveform is nominally 100 kHz and may pause indefinitely between pulses;
- Intel 8086 documentation specifies a minimum processor clock of 2 MHz because internal state uses dynamic storage, and explicitly states that single-step/single-cycle operation cannot be implemented by stopping the clock.

Therefore:

> **The current `CLOCK_STEPPED` mode is not an Intel P8086-2 compliant general execution contract.**

The same audit also affects the existing 1 MHz `FREE_RUNNING` Intel configuration:

> **A 1 MHz free-running physical pass is valid empirical evidence, but it is still below the Intel 8086 documented 2 MHz minimum and must not be presented as a vendor-compliant operating point.**

For an Intel 8086 on the present HAT, compliant general execution requires a continuously running clock at or above the documented minimum and a processor-bus responder that can satisfy every active cycle without relying on arbitrary M33 software latency. If variable-latency cycles are required, controllable `READY` or equivalent hardware support is needed.

This finding is relevant to FreeRTOS Issue #75 because all current software execution and companion transaction models pass while the physical failure remains. It is a demonstrated operating-contract violation, but it is **not by itself a proven root cause** of #75.

---

## 1. Physical hardware boundary

The current Pi86 HAT is retained without PCB redesign. Its fixed processor connections include:

```text
Processor READY (pin 22) -> 3.3 V
```

As a result, the RP2350 does not own `READY` and cannot extend an Intel bus cycle by requesting a standard wait state.

The consequence is architectural rather than cosmetic:

```text
8086 active bus cycle
        |
        +-- READY controllable? -- no
        |
        +-- response must therefore be available by the fixed bus deadline
```

Any design that needs an arbitrary software decision after the processor has already entered an active cycle cannot rely on ordinary Intel wait-state insertion on this hardware.

Canonical hardware references in this repository:

- `docs/architecture/hardware.md`
- `docs/adr/0003-define-physical-timing-boundary.md`
- `firmware/bus/processor_bus_pins.h`

---

## 2. Existing clock modes

The runtime currently exposes two Execution Clock Modes.

### 2.1 FREE_RUNNING

`FREE_RUNNING` keeps the processor clock running and depends on prepared realtime state, PIO/DMA, or another bounded responder to satisfy active-cycle timing.

The canonical firmware is currently built with:

```text
RP86_PROCESSOR_HZ = 1,000,000
```

Physical Intel P8086-2 validation at 1.000 MHz exists and remains useful experimental evidence. However, because 1 MHz is below the Intel 8086 documented 2 MHz minimum, it is not a vendor-compliant operating point.

The correct interpretation is:

```text
1 MHz physical PASS
    = this sample completed this measured workload
    != Intel-guaranteed operating contract
```

### 2.2 CLOCK_STEPPED

The current PIO program is:

```pio
pull block
set pins, 1 [9]
set pins, 0 [9]
mov isr, null
push block
```

The PIO state machine runs at 2 MHz. The source deliberately holds each clock level for 5 us, producing a nominal 100 kHz processor waveform, and `pull block` may leave `CLK` low for an arbitrary firmware-controlled interval between pulses.

The mode exists specifically so M33 software can inspect a completed phase and service general memory or I/O before issuing the next pulse.

That design is internally coherent as a software state machine, but it is incompatible with the Intel 8086 minimum-clock requirement.

---

## 3. Why increasing the stepped clock constant is not a fix

The problem is not merely that 100 kHz is numerically lower than 2 MHz.

The current architecture is:

```text
issue one complete pulse
        |
        v
CLK low
        |
        v
M33 inspects / services transaction
        |
        v
software eventually authorizes next pulse
```

Even if the high/low pulse width were changed so the pulses themselves corresponded to 2 MHz, `pull block` would still allow an unbounded low interval between pulses.

Therefore:

> **`CLOCK_STEPPED` cannot become Intel-compliant by changing only a divider or delay constant.**

The service architecture must change.

---

## 4. Intel-compliant choices on the current HAT

Because `READY` is fixed asserted, the current HAT leaves one practical Intel-compliant execution class.

### Continuous-clock prepared/realtime execution

```text
Intel 8086 CLK >= documented minimum
             |
             v
       active bus cycle
             |
             v
PIO / DMA / prepared state / bounded realtime responder
             |
             v
       cycle completes
```

Requirements:

1. `CLK` runs continuously at or above the Intel minimum.
2. Every supported memory, I/O, and INTA transaction has a bounded response path.
3. M33 foreground software, USB, filesystems, storage lookup, and arbitrary Host latency are outside the current-cycle deadline.
4. Unsupported cycles fail observably rather than waiting for unbounded software service.

This is conceptually the existing `FREE_RUNNING` architectural model, but the Intel operating point itself must be moved into specification before it is called compliant.

---

## 5. What the current HAT cannot provide for Intel

The unmodified HAT cannot simultaneously provide all three of the following:

```text
1. Intel-compliant continuous minimum clock
2. arbitrary per-cycle M33 software latency
3. standard processor wait-state insertion
```

because item 3 requires ownership of `READY`, which the HAT does not expose to the RP2350.

Therefore general address-indexed memory or I/O that cannot meet the continuous-clock bus deadline needs one of these architectural changes:

- make `READY` controllable through revised hardware;
- add external wait-state / bus-control logic;
- move the service entirely into a realtime PIO/DMA/hardware responder;
- stage/cache all processor-visible state into a bounded fast path and define deterministic miss behavior.

A Host or M33 callback that simply waits longer while stopping Intel `CLK` is not an acceptable replacement.

---

## 6. Relationship to existing ADRs

### ADR 0003

ADR 0003 correctly records the fixed-`READY` boundary and the rule that current-cycle behavior must come from prepared/bounded state.

That boundary remains valid.

### ADR 0010

ADR 0010 accepted `CLOCK_STEPPED` as an empirical project capability and explicitly noted that Intel 8086 / NEC V30 were not being represented as vendor-guaranteed fully static processors.

Issue #102 now makes the Intel consequence explicit:

> For the installed Intel P8086-2, arbitrary stopped-clock `CLOCK_STEPPED` operation is outside the documented operating contract.

ADR 0010 therefore needs an Intel-specific amendment or superseding decision before `CLOCK_STEPPED` can remain a canonical Intel execution mode.

No firmware behavior is changed by this audit document.

---

## 7. Historical physical evidence

Existing physical results obtained at 1 MHz free-running or under clock-stepped operation remain factual measurements of those exact experiments.

They should be described as:

- physically observed;
- repeatable where evidence exists;
- useful for bring-up and diagnosis;
- outside the Intel documented minimum-clock operating contract when below 2 MHz or when the clock is arbitrarily stopped.

They should not be upgraded into a general Intel reliability guarantee.

This distinction prevents two opposite mistakes:

1. discarding valid empirical evidence merely because it is out of specification; or
2. treating an empirical pass as proof that the processor is guaranteed to retain state under unsupported clocking.

---

## 8. FreeRTOS #75 relevance

The #75 investigation has progressively eliminated software-only explanations as independently sufficient causes:

```text
real delayed-list machine code             PASS
real xQueueReceive caller context          PASS
real initial task-frame restore            PASS
real tick ISR / IRET at suspect points     PASS
RP2350 executor/controller transaction     PASS
```

The remaining uncertainty lies below those models, at the physical processor/bus timing boundary.

The Intel clock-contract violation is therefore a strong architectural finding because it is located exactly at the remaining unmodeled boundary.

However:

> **Out-of-spec clocking is not yet proven to be the specific cause of the #75 stall.**

A physical #75 comparison is meaningful only after the Intel test is executed under a compliant clock contract.

Until then, the maintained finite 1000 ms receive timeout remains a workaround, not proof of the underlying cause.

---

## 9. NEC V30 boundary

Intel 8086 requirements must not be silently projected onto NEC V30, and NEC behavior must not be used to waive Intel requirements.

For the current project:

```text
Intel 8086 = primary architectural baseline
NEC V30    = compatibility target
```

The V30 clock-stop/minimum-frequency contract requires its own vendor-document audit before any claim of static or arbitrarily stoppable operation is made.

---

## 10. Architectural decision boundary

For Intel 8086 work, the project should proceed with this rule:

```text
                   Intel 8086
                       |
          CLK continuously in spec
                       |
          +------------+------------+
          |                         |
    prepared/realtime hit      unsupported/miss
          |                         |
          v                         v
      complete cycle           observable fault
```

Not:

```text
active Intel bus work
        |
        v
stop CLK indefinitely
        |
        v
wait for M33 / Host / storage
        |
        v
resume processor
```

If general variable-latency bus service remains a project requirement, the next hardware architecture must expose a legal processor wait mechanism or implement the full service path within the realtime response budget.

---

## 11. Next actions

1. Amend or supersede ADR 0010 for the Intel 8086 path.
2. Reclassify current 1 MHz `FREE_RUNNING` and stopped-clock `CLOCK_STEPPED` Intel evidence as empirical/out-of-spec rather than canonical compliant operating modes.
3. Define the minimum compliant Intel `FREE_RUNNING` operating point and verify PIO/DMA/bus timing at that rate.
4. Audit which current processor-visible memory and I/O services can satisfy the continuous-clock deadline.
5. Identify services that depend on M33 software between cycles and classify them as unsupported for compliant Intel execution unless moved into a realtime responder.
6. Keep hardware redesign / controllable `READY` as the explicit option for general variable-latency execution.
7. Re-run the #75 physical comparison only after the Intel clock contract is compliant.
8. Audit NEC V30 clock requirements separately.

## Related

- Issue #75 — FreeRTOS indefinite-block physical stall
- Issue #98 / PR #101 — RP2350 companion interrupt-pipeline audit
- Issue #102 — Intel P8086-2 clock-contract audit
- ADR 0003 — Physical timing boundary
- ADR 0010 — FREE_RUNNING / CLOCK_STEPPED execution modes
