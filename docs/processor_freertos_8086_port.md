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

The v1 RTOS port adds a stronger runtime invariant: **every execution context that enters FreeRTOS C runs with `SS=DS=DGROUP`**. This includes the pre-scheduler bootstrap stack, the kernel idle task, and every application task. Each TCB therefore stores the ordinary FreeRTOS near `pxTopOfStack`, which is exactly the task's 16-bit `SP`.

This invariant is required because Open Watcom `-zu` correctly represents addresses of automatic stack objects as SS-relative far pointers, while unmodified FreeRTOS APIs use ordinary near data pointers. The compiler can warn when such a pointer is truncated. Under the port invariant the segment component is exactly DGROUP, so the retained offset names the same object. The port does not rely on an arbitrary SS being silently truncated to DS.

The manifest-provided initial stack is used only long enough to establish DS/ES and clear BSS. Startup then switches to a dedicated bootstrap stack inside DGROUP before calling any FreeRTOS C function. Scheduler-created task stacks are allocated from the DGROUP heap and execute with the same SS=DS invariant.

This is intentionally narrower than the full 256 KiB RP86 processor-visible memory map. Kernel data, heap, TCBs, queue/semaphore objects, bootstrap stack, and task stacks must fit inside the 64 KiB DGROUP budget.

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

## Compiler helpers

The build intentionally uses `option nodefaultlibs`, so no DOS C runtime is linked. Open Watcom's 16-bit code generator can lower near `memcpy`/`memset` operations to its internal `memcpy_` and `memset_` helper ABI. RP86 supplies small project-owned 8086 implementations in `compiler_helpers.asm` using the compiler-documented register contract:

```text
memcpy_: DI=dst, SI=src, CX=len -> DI=original dst; clobbers ES,SI,CX
memset_: DI=dst, AL=value, CX=len -> DI=original dst; clobbers ES,CX
```

These helpers operate entirely inside DGROUP and do not introduce a DOS or BIOS dependency.

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
