# FreeRTOS 8086 portable layer

Status: **Issue #60 implementation contract; physical Intel 8086 is the primary acceptance target.** NEC V30 remains a final compatibility regression under #61.

## Boundary

FreeRTOS is a native processor workload. It does not run on the RP2350 and does not replace RP86:

```text
Host -> RP2350 runtime -> physical Intel 8086 -> FreeRTOS -> native tasks
```

The kernel is pinned as the upstream `FreeRTOS/FreeRTOS-Kernel` submodule at commit `8be86d4a24fd4091f8f4192018423ab590f408db`, the same source revision audited in #62. Upstream kernel code remains unmodified; RP86-specific code lives under `processor/ports/freertos/8086/`.

## C and memory model

Issue #58 fixed the processor C ABI to Open Watcom C/16 with `-0 -ms -ecc -zu`: pure Intel-8086 code generation, small code/small data, near ordinary pointers, `__cdecl`, `DS=DGROUP`, and no compiler assumption that `SS==DS`.

The v1 RTOS port makes one additional scheduler-level decision: **all task stacks live in DGROUP and execute with `SS=DGROUP`**. Each TCB therefore stores the ordinary FreeRTOS near `pxTopOfStack`, which is exactly the task's 16-bit `SP`. This avoids inventing out-of-band segment metadata and stays inside the near-pointer contract from #58. `-zu` remains enabled; the compiler still does not assume `SS==DS` as an ABI rule.

This is intentionally narrower than the full 256 KiB RP86 processor-visible memory map. Kernel data, heap, TCBs, queue/semaphore objects, and initial task stacks must fit inside the 64 KiB DGROUP budget.

## Saved context

A hardware tick or software yield first creates the 8086 interrupt frame `FLAGS, CS, IP`. Project-owned NASM then pushes the remaining architectural state in this order:

```text
AX BX CX DX ES DS SI DI BP
```

Because the 8086 stack grows downward, the saved stack starting at the TCB's `pxTopOfStack` is:

```text
BP DI SI DS ES DX CX BX AX IP CS FLAGS
```

An initial task stack appends an ordinary near-C entry frame above that interrupt frame:

```text
... IP CS FLAGS return-IP pvParameters
```

`portasm.asm` restores `BP, DI, SI, DS, ES, DX, CX, BX, AX` and finishes with `IRET`. Initial FLAGS are `0202h`, enabling maskable interrupts while retaining the architectural reserved bit.

## Interrupt and yield mapping

The RP2350-owned periodic source from #59 remains 100 Hz wall-clock time and enters the workload through physical `INTR`, two-cycle `INTA`, and vector `21h`. The tick wrapper saves the explicit frame, calls `xTaskIncrementTick()`, calls `vTaskSwitchContext()` when required, emits RP86 EOI with `OUT 20h,20h`, restores the selected task, and executes `IRET`.

Voluntary yield uses workload-owned software vector `80h`. `INT 80h` creates the same `FLAGS/CS/IP` shape, but the software path does not emit RP2350 EOI.

`CLD` is executed before entering kernel C from either ISR path. A task's original direction flag remains in its interrupt-return FLAGS and is restored by `IRET`.

## Critical sections

The Open Watcom port macro uses the historical 8086-safe `PUSHF; CLI` / `POPF` model. The saved FLAGS word lives on the current task stack, so nested critical sections preserve the previous interrupt-enable state without a global nesting variable.

## Terminal idle boundary

The FreeRTOS idle task does not use `HLT` in this stage. Ordinary RTOS idle remains a schedulable spin/yield state. RP86's existing `IDLE_PREPARE + HLT` sequence remains reserved for terminal workload completion.

## Acceptance progression

The implementation is accepted in layers: first compile/link the upstream kernel against the project-owned port, then prove first-task launch and voluntary switching, then prove periodic preemption, then prove queue/semaphore block-and-wake behavior, and finally run sustained physical Intel 8086 regression. NEC V30 uses the same common 8086 image path later in #61.
