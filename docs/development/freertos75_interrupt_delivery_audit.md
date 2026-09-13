# FreeRTOS #75 — RP2350 Interrupt Delivery Audit

## Purpose

This document records the read-only state-machine audit requested by issue #98.
It begins after the IA16 software-execution work in #86, #94, and #96 established that the production Intel 8086 code path is internally coherent through:

```text
first-task restore
    -> prvConsumerTask
    -> xQueueReceive
    -> vTaskPlaceOnEventList
    -> prvAddCurrentTaskToDelayedList
    -> xSuspendedTaskList insertion
```

The remaining question is whether the **RP2350 companion-side periodic-interrupt pipeline** can present a state ordering to the physical Intel 8086 that is not represented by the currently separate software tests.

This audit does not change interrupt behavior, timing constants, or FreeRTOS source.

---

## 1. Ownership

The periodic tick path is split across four owners.

### `firmware/runtime/workload_executor.c`

Owns the policy state:

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
- the delivery gates;
- logical INTA #1 / INTA #2 progression in the clock-stepped service path;
- real-EOI versus yield-only restore-fence handling.

### `firmware/bus/processor_bus.c`

Owns the physical `INTR` GPIO level through:

```text
rp86_processor_bus_set_intr(bool asserted)
```

The function is deliberately thin: policy remains in the executor.

### `firmware/runtime/processor_service.pio`

PIO1 SM3 owns the physical two-cycle interrupt acknowledge transaction:

```text
INTA #1 asserted
    -> do not drive AD
    -> PIO IRQ4
    -> wait for INTA #1 completion

INTA #2 asserted
    -> drive encoded vector on AD
    -> release AD when INTAK deasserts
    -> PIO IRQ5
```

The documented ownership rule is that Core0 may deassert physical `INTR` after IRQ4, while the PIO state machine owns AD during the vector phase.

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

The same EOI-port write has two meanings on the RP2350 side:

- if `tick_in_service == true`: complete the real hardware tick;
- otherwise: scheduler resume fence only.

---

## 2. Logical States

The executor state can be usefully reduced to the following logical phases.

### S0 — Idle

```text
tick_pending        = 0
tick_intr_asserted  = 0
tick_ack_phase      = 0
tick_in_service     = 0
```

No periodic request is waiting or active.

### S1 — Pending but gated

```text
tick_pending        = 1
tick_intr_asserted  = 0
tick_ack_phase      = 0
tick_in_service     = 0
```

A wall-clock period has produced a request, but one or both delivery gates are closed.

### S2 — INTR asserted, not yet accepted

```text
tick_pending        = 1
tick_intr_asserted  = 1
tick_ack_phase      = 0
tick_in_service     = 0
```

The request is physically visible on `INTR`, but the software model has not yet accepted INTA #1.

### S3 — INTA #1 accepted

```text
tick_pending        = 0
tick_intr_asserted  = 0
tick_ack_phase      = 1
tick_in_service     = 0
```

The interrupt request has been accepted. `INTR` is deasserted and the second acknowledge cycle owns the next transition.

### S4 — Tick ISR in service

```text
tick_pending        = 0
tick_intr_asserted  = 0
tick_ack_phase      = 0
tick_in_service     = 1
```

INTA #2 delivered vector `21h`; the processor is executing the tick ISR until the common restore EOI.

A source period expiring in S3/S4 is accounted as delayed/coalesced rather than arming an immediate second request.

---

## 3. Main Transitions

```text
S0
 | 100 Hz source expires
 v
S1
 | wall-time gate open AND progress gate open
 v
S2
 | INTA #1 accepted
 v
S3
 | INTA #2 vector delivered
 v
S4
 | real hardware-tick EOI
 v
S0 + wall-time recovery gate + progress recovery gate
```

A pending request may also take this path:

```text
S2
 | yield-only common restore fence
 | only if INTA has NOT started
 v
S1
```

The request remains pending, physical `INTR` is retracted, and only the processor-progress gate is refreshed.

---

## 4. Required Invariants

The following invariants should hold outside momentary function-call implementation details.

### I1 — asserted INTR implies a retractable pending request

```text
tick_intr_asserted
    => tick_pending
    && tick_ack_phase == 0
    && !tick_in_service
```

### I2 — after INTA #1 the request is no longer retractable

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

A wall-clock period that expires here is delayed/coalesced and does not become an immediate pending request.

### I5 — yield-only fence never consumes a request

If:

```text
!tick_in_service
&& tick_ack_phase == 0
&& tick_intr_asserted
```

then the common restore fence may retract physical `INTR`, but it must preserve:

```text
tick_pending == true
```

### I6 — real EOI starts both recovery gates

A real tick EOI establishes:

```text
tick_delivery_not_before_us
    = current wall time + 10 ms
```

and:

```text
tick_delivery_not_before_cycle
    = pre-commit bus_stats.cycles + 64 + 1
```

Because `io_write()` runs before the EOI bus cycle is committed, the `+1` accounts for the current fence cycle. After that cycle is committed, the observable remaining budget is 64 completed processor cycles.

### I7 — yield-only fence refreshes only processor progress

A voluntary-yield restore fence must not restart the 100 Hz wall-time phase. It refreshes only:

```text
tick_delivery_not_before_cycle
```

Otherwise a tight `taskYIELD()` loop could postpone wall-clock delivery indefinitely.

---

## 5. Existing Test Coverage

### Executor policy

`tests/firmware/runtime/test_periodic_tick_executor.c` covers:

- first source-period generation;
- transition S1 -> S2;
- modeled INTA #1: S2 -> S3;
- modeled INTA #2: S3 -> S4;
- real EOI and both recovery gates;
- pending request blocked by the progress gate;
- asserted-but-not-yet-accepted request retracted by a yield-only fence;
- pending request retained across that retraction;
- long-CLI coalescing;
- source expiry while a tick is in service;
- simultaneous wall-time and cycle-gate requirements;
- terminal shutdown of the periodic source.

The test explicitly verifies that after a committed EOI cycle the progress deadline is 64 cycles ahead, matching the pre-commit `+65` expression in production code.

### IA16 processor-side context

Issue #96 / PR #97 covers the production processor side:

- real task-frame construction and first restore;
- real `xQueueReceive()` path;
- delayed-list transition;
- controlled tick injection at all 22 instructions in the physical suspect window;
- register/segment/stack/list preservation across the real tick ISR and IRET.

This strongly reduces the probability of a pure IA16 context-save/restore defect.

### PIO acknowledge transaction

`firmware/runtime/processor_service.pio` defines the physical transaction contract itself:

```text
INTA #1 -> IRQ4, no AD drive
INTA #2 -> vector drive -> AD release -> IRQ5
```

Existing physical/bring-up work has validated the two-cycle INTA mechanism, but this is not the same as composing it with the periodic-tick executor state machine in one host-side regression test.

---

## 6. Demonstrated Coverage Gap

The current test split leaves one important composition gap.

`test_periodic_tick_executor.c` models an interrupt acknowledge by calling the executor's `interrupt_ack()` callback directly. This proves the executor transition logic, but it does not model the asynchronous boundary introduced by the physical PIO responder:

```text
physical INTA #1 edge
    -> PIO SM3 observes it
    -> PIO IRQ4 becomes pending
    -> Core0 observes IRQ4
    -> executor/Core0 deasserts INTR / advances software state
```

The PIO program separately proves the intended bus ownership, but no single software regression currently binds these two views into one ordered transaction.

Therefore the remaining software question is not whether either half works independently. It is:

> Can the composed executor + PIO event ordering admit a transient state that violates the logical invariants above?

The most important boundary is S2 -> S3, because physical request acceptance occurs in PIO before Core0 necessarily processes the IRQ4 witness.

This is an audit/test-coverage gap, not evidence of a defect.

---

## 7. Race Analysis

### Yield-fence retraction versus INTA #1

At the policy level a yield-only fence may retract `INTR` only while:

```text
tick_ack_phase == 0
&& !tick_in_service
```

Once the software model has processed INTA #1, retraction is forbidden.

The physical processor also constrains the race: once the 8086 begins an interrupt acknowledge sequence, the interrupted foreground task is no longer executing a later yield-fence instruction. That makes a true processor-side ordering of:

```text
INTA #1 accepted
then old-task EOI/yield fence
```

architecturally implausible.

However, the RP2350 has two observation domains (PIO and Core0), so a host-side composed test should still make the firmware-side ordering explicit instead of relying on this argument alone.

### 64-cycle progress gate

The production expression:

```text
bus_stats.cycles + 64 + 1
```

is internally consistent with the current service ordering because the EOI write callback executes before the current bus cycle is committed to `bus_stats.cycles`.

The existing periodic-tick test independently confirms the post-commit result:

```text
deadline == committed_cycles + 64
```

No off-by-one defect is demonstrated here.

---

## 8. Next Test

The next implementation step should be a **cross-layer interrupt transaction harness**, not a timing-constant change.

The harness should model the observable PIO/Core0 events explicitly:

```text
1. executor generates tick
2. executor asserts physical INTR
3. simulated PIO observes INTA #1
4. PIO publishes IRQ4
5. Core0 consumes IRQ4 and advances executor acceptance state
6. simulated PIO observes INTA #2
7. vector 21h is driven
8. PIO publishes IRQ5
9. executor enters in-service state
10. processor-side EOI/fence is delivered
11. recovery gates are established
12. pending/reassertion policy is checked
```

Include the yield-fence case where a request became asserted just before the fence, and force ordering at every legal point before INTA #1 acceptance.

The test should assert the invariants in Section 4 after every transition.

Only if this composed test demonstrates a reachable invariant violation should production behavior be changed.

---

## 9. Current Conclusion

No source-level contradiction or off-by-one error is established by this read-only audit.

The strongest remaining software uncertainty is the **cross-domain ordering between executor policy state and the PIO INTA witness path**. Existing tests cover the two halves well but do not yet compose them into one transaction-level regression.

That is the next narrow test target for issue #98.
