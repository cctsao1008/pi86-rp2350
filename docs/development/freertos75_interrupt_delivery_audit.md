# FreeRTOS #75 — RP2350 Interrupt Delivery Audit

## Purpose

This document records the read-only state-machine audit requested by issue #98.
It begins after the IA16 software-execution work in #86, #94, and #96 established that the production Intel 8086 code path is internally coherent through first-task restore, `xQueueReceive()`, delayed-list insertion, and controlled tick interleaving.

A critical scope correction emerged during this audit:

> **The #75 PMAX workload is `clock-stepped`. Its periodic INTA path is serviced by `workload_executor.c` + `clock_stepped_bus_controller.c` + `processor_bus.c`. The PIO1 SM3 responder in `processor_service.pio` is not the acknowledge engine used by this workload path.**

`workload-pmax-a.json` explicitly selects:

```json
"clock": "clock-stepped",
"flags": ["periodic-tick"]
```

and `rp86_workload_executor_service()` only services an active general workload when `clock_mode == RP86_WORKLOAD_CLOCK_STEPPED`, then calls `rp86_clock_stepped_service_cycle()`.

This removes the initially suspected PIO-IRQ4/IRQ5-to-Core0 asynchronous ordering from the #75 software path. The remaining audit target is the composition of the **executor tick policy with the real clock-stepped bus controller and processor-bus acknowledge completion**.

No production interrupt behavior, timing constant, or FreeRTOS source is changed by this audit.

---

## 1. Actual #75 Interrupt Ownership

### `firmware/runtime/workload_executor.c`

Owns the periodic-tick policy state:

```text
tick_pending
tick_intr_asserted
tick_ack_phase
tick_in_service
tick_delivery_not_before_us
tick_delivery_not_before_cycle
```

It also owns:

- 100 Hz wall-clock generation;
- coalescing/delay accounting;
- delivery gates;
- the logical INTA #1 / INTA #2 policy callback;
- real-EOI versus yield-only restore-fence handling.

### `firmware/runtime/clock_stepped_bus_controller.c`

Owns one complete clock-stepped bus transaction at the runtime level.

For an interrupt-acknowledge cycle it performs:

```text
processor_bus_wait_cycle()
    -> classify INTERRUPT_ACK
    -> executor interrupt_ack callback
    -> processor_bus_complete_interrupt_ack(drive_vector, vector)
    -> commit stats.interrupt_acks / stats.cycles
```

This is the production composition boundary missing from the existing executor-only policy test.

### `firmware/bus/processor_bus.c`

Owns the physical clock-stepped bus signals.

Relevant functions are:

```text
rp86_processor_bus_set_intr()
rp86_processor_bus_wait_cycle()
rp86_processor_bus_complete_interrupt_ack()
```

`rp86_processor_bus_wait_cycle()` classifies a bus cycle as `INTERRUPT_ACK` whenever sampled `INTA` is asserted, independent of normal A0/BHE lane decoding.

`rp86_processor_bus_complete_interrupt_ack()`:

- leaves AD high-Z for INTA #1 (`drive_vector == false`);
- drives the low-byte vector for INTA #2 (`drive_vector == true`);
- advances two execution-clock steps to complete the current acknowledge cycle;
- always releases AD afterwards.

### `processor/ports/freertos/8086/portasm.asm`

Owns processor-side context save/restore and the common restore fence:

```text
hardware tick ISR
or
INT 80h voluntary yield
    -> restore selected task context
    -> OUT PIC EOI / scheduler-resume fence
    -> IRET
```

The same EOI-port write has two RP2350 meanings:

- if `tick_in_service == true`: complete the real hardware tick;
- otherwise: scheduler-resume fence only.

### `firmware/runtime/processor_service.pio` — not the #75 path

PIO1 SM3 implements a separate physical two-cycle INTA responder used by the prepared/canonical service runtime. Its IRQ4/IRQ5 witness contract is valid for that runtime, but it is **not** the interrupt-acknowledge mechanism traversed by the PMAX-A clock-stepped general workload.

It must therefore not be used to infer a PIO/Core0 race for #75.

---

## 2. Logical Executor States

### S0 — Idle

```text
tick_pending        = 0
tick_intr_asserted  = 0
tick_ack_phase      = 0
tick_in_service     = 0
```

### S1 — Pending but gated

```text
tick_pending        = 1
tick_intr_asserted  = 0
tick_ack_phase      = 0
tick_in_service     = 0
```

### S2 — INTR asserted, not yet accepted

```text
tick_pending        = 1
tick_intr_asserted  = 1
tick_ack_phase      = 0
tick_in_service     = 0
```

### S3 — INTA #1 accepted

```text
tick_pending        = 0
tick_intr_asserted  = 0
tick_ack_phase      = 1
tick_in_service     = 0
```

### S4 — Tick ISR in service

```text
tick_pending        = 0
tick_intr_asserted  = 0
tick_ack_phase      = 0
tick_in_service     = 1
```

The normal transition is:

```text
S0 --100 Hz expiry--> S1
S1 --delivery gates open--> S2
S2 --clock-stepped INTA #1 cycle--> S3
S3 --clock-stepped INTA #2 cycle--> S4
S4 --processor EOI write--> S0 + recovery gates
```

A yield-only restore fence may instead perform:

```text
S2 -> S1
```

but only before an INTA cycle has been accepted. The pending request is retained while physical `INTR` is retracted and the processor-progress gate is refreshed.

---

## 3. Required Invariants

### I1 — asserted INTR is still a pending request

```text
tick_intr_asserted
    => tick_pending
    && tick_ack_phase == 0
    && !tick_in_service
```

### I2 — INTA #1 consumes pending/retractable state

```text
tick_ack_phase == 1
    => !tick_pending
    && !tick_intr_asserted
    && !tick_in_service
```

### I3 — acknowledge phase and ISR service are mutually exclusive

```text
tick_ack_phase != 0
    => !tick_in_service
```

### I4 — in-service state owns the real EOI

```text
tick_in_service
    => tick_ack_phase == 0
    && !tick_intr_asserted
```

### I5 — yield-only fence never consumes pending work

If a fence retracts an asserted but unaccepted request, it must preserve:

```text
tick_pending == true
```

### I6 — real EOI starts both recovery gates

A real tick EOI establishes:

```text
tick_delivery_not_before_us
    = current wall time + 10 ms
```

and before the current EOI bus cycle is committed:

```text
tick_delivery_not_before_cycle
    = bus_stats.cycles + 64 + 1
```

After that bus cycle is committed, the remaining observable budget is 64 completed processor cycles.

### I7 — yield-only fence refreshes only processor progress

A voluntary-yield restore fence must not restart the wall-clock gate. Otherwise a tight yield loop could indefinitely move the 100 Hz source delivery boundary.

### I8 — each accepted clock-stepped INTA cycle is atomic at executor service granularity

Within one call to `rp86_clock_stepped_service_cycle()` the order is:

```text
wait/classify cycle
    -> executor interrupt_ack callback changes logical phase
    -> complete that physical acknowledge cycle
    -> commit cycle statistics
```

No second processor bus transaction, including an EOI write, is serviced between the callback and completion of that same acknowledge cycle.

This is materially different from the PIO/Core0 asynchronous model originally considered for #98.

---

## 4. Existing Test Coverage

### Executor policy

`tests/firmware/runtime/test_periodic_tick_executor.c` already covers:

- source generation;
- S1 -> S2 assertion;
- logical INTA #1 and #2 callback transitions;
- real EOI and both recovery gates;
- pending request blocked by the progress gate;
- yield-only retraction before acceptance;
- pending retention across retraction;
- long-CLI coalescing;
- source expiry while in service;
- simultaneous wall-time and cycle-gate requirements;
- terminal periodic-source shutdown.

The test explicitly confirms that the pre-commit `+65` expression becomes exactly `committed_cycles + 64` after the EOI cycle is counted. No off-by-one defect is demonstrated.

### Clock-stepped INTA controller

`tests/firmware/runtime/test_clock_stepped_interrupt_ack.c` exercises the production `rp86_clock_stepped_service_cycle()` with an `INTERRUPT_ACK` bus cycle and verifies:

- INTA #1 callback result completes without vector drive;
- INTA #2 callback result drives vector `21h`;
- acknowledge and cycle counters are committed;
- missing callback and failed physical completion are rejected.

### IA16 processor-side context

Issue #96 / PR #97 covers:

- real task-frame construction / first restore;
- real `xQueueReceive()` path;
- real delayed-list insertion;
- real tick ISR/IRET injected at all 22 instructions in the physical suspect window;
- architectural state preservation.

---

## 5. Demonstrated Coverage Gap

The remaining software gap is narrower than the original #98 wording suggested.

Two tests cover the two relevant halves:

```text
test_periodic_tick_executor.c
    executor state machine

 test_clock_stepped_interrupt_ack.c
    real clock-stepped INTA transaction controller
```

but they are not composed in one test. The executor test replaces `rp86_clock_stepped_service_cycle()` with a test stub, while the controller test supplies an independent synthetic acknowledge policy callback.

Therefore no regression currently proves the exact production composition:

```text
workload_executor_service()
    -> update_periodic_tick()
    -> real rp86_clock_stepped_service_cycle()
    -> real executor interrupt_ack callback
    -> processor_bus_complete_interrupt_ack()
    -> executor state / bus-stats commit
```

This is a composition-test gap, not evidence of a defect.

---

## 6. Race Analysis

### Yield fence versus INTA #1

For the #75 clock-stepped path, the bus controller serializes complete bus cycles. Once an INTA #1 cycle has been classified, the executor callback transitions S2 -> S3 and that physical cycle is completed before the next processor bus transaction can be serviced.

A processor EOI/yield write is therefore not interleaved *inside* the INTA #1 service transaction.

The legal alternatives are:

```text
yield EOI bus cycle first
    -> request may be retracted S2 -> S1

or

INTA #1 bus cycle first
    -> request becomes S3 and is no longer retractable
```

This substantially weakens the original concern about an asynchronous PIO witness window for #75 because that window belongs to a different runtime path.

### 64-cycle progress gate

The production ordering remains internally consistent:

```text
io_write(EOI) callback
    -> records pre-commit cycles + 65
    -> service cycle returns
    -> current EOI cycle increments stats.cycles
```

The post-commit deadline is therefore 64 cycles ahead, as the current test asserts.

---

## 7. Next Test

Add one **production-composition host regression** that links:

```text
workload_executor.c
clock_stepped_bus_controller.c
```

while stubbing only the lowest physical `processor_bus_*` primitives.

Feed an explicit sequence of physical bus-cycle observations:

```text
normal progress
100 Hz expiry / INTR assertion
INTA #1
INTA #2
processor ISR progress
PIC EOI write
64-cycle recovery
pending reassertion
```

and separately:

```text
INTR asserted
PIC EOI yield fence BEFORE INTA #1
request retained / INTR retracted
64-cycle progress
INTA #1
INTA #2
```

The test should assert I1-I8 after every completed transaction.

The key point is that the test must use the real `rp86_clock_stepped_service_cycle()` and the real private executor `interrupt_ack`/`io_write` callbacks through `rp86_workload_executor_service()`. It should not invoke those private callbacks directly.

Only if this composed production path demonstrates a reachable invariant violation should runtime behavior be changed.

---

## 8. Current Conclusion

The audit found an important architectural correction rather than a tick-state defect:

> The #75 PMAX workload does not traverse the PIO1 SM3 / IRQ4 / IRQ5 acknowledge path. It uses the synchronous clock-stepped bus controller.

Consequently the originally suspected PIO/Core0 cross-domain race is not a valid #75 hypothesis.

No source-level state contradiction, recovery-gate off-by-one, or IA16 context failure is currently demonstrated. The remaining software uncertainty is the untested composition of the executor state machine with the real clock-stepped bus controller.

That production-composition regression is the next narrow target for issue #98.
