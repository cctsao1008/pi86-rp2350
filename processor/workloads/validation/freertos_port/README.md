# FreeRTOS 8086 port validation

This workload is the Issue #60 integration witness for the project-owned FreeRTOS portable layer. It links the pinned upstream FreeRTOS kernel with Open Watcom C/16 and NASM without DOS, BIOS, PIT, or 8259 dependencies.

The v1 port has one deliberate small-model invariant: every FreeRTOS C frame executes with `SS=DS=DGROUP`. Startup clears BSS using the manifest stack, switches to a dedicated DGROUP bootstrap stack before entering kernel C, and scheduler-created task stacks come from the DGROUP heap. This makes the unmodified kernel's near data-pointer API compatible with Open Watcom `-zu` stack-local addressing without assuming an arbitrary external stack segment equals DS.

Open Watcom may lower `memcpy` and `memset` operations to register-ABI compiler helpers. The workload supplies pure-8086 `memcpy_`/`memset_` implementations rather than linking the DOS C runtime.

On the physical Intel 8086 the workload must prove, in one run:

1. `vTaskStartScheduler()` enters the first task through the fabricated IRET frame;
2. two equal-priority tasks survive repeated voluntary `taskYIELD()` / `INT 80h` context switches;
3. one task reaches a no-yield rendezvous where only the RP2350-owned vector `21h` tick can let the peer run, proving periodic preemption;
4. an empty binary semaphore blocks Task A and Task B wakes it;
5. an empty queue blocks Task A and Task B sends the expected `5A3Ch` value;
6. native success publishes result `6060h`, emits exact `RESULT: PASS`, and terminates with `IDLE_PREPARE + HLT`.

Failure codes use the `60xxh` range and distinguish creation, synchronization, queue, preemption, and scheduler-return failures.

The FreeRTOS idle task remains the ordinary kernel idle loop; the RP86 terminal HLT sequence is used only by the validation completion helper.
