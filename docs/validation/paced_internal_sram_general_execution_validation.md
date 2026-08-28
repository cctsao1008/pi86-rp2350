# PACED Internal-SRAM General Execution Validation

**Date:** 2026-08-28
**Result:** PASS
**Target:** `paced_native_runtime`
**Hardware:** RP2350 PiZero2Lab plus the unmodified Pi86 HAT and an installed 8086-class physical processor

## Purpose

This validation answers one physical question:

> Can the RP2350 serve general native code, writable memory, stack traffic, and
> I/O to a real 8086-class processor when the Pi86 HAT keeps `READY` asserted?

The tested answer is yes when the RP2350 owns a software-paced clock. It issues
one complete clock pulse at a time and may remain at `CLK=LOW` between pulses
while M33 firmware services the completed bus cycle.

This is native processor execution. The RP2350 does not interpret x86
instructions.

## Test image

The 261-byte flat NASM workload is loaded at physical address `10000h` and entered
at `1000:0000`. The stack top is `3000:FFFE` (physical `3FFFEh`). A reset handoff
at `FFFF0h` performs a far jump to the workload.

The workload deliberately exercises:

- taken conditional branches;
- the native `LOOP` instruction;
- `PUSH` and `POP` stack traffic;
- byte and word Internal-SRAM reads and writes;
- a native arithmetic result, sum `1..10 = 55`;
- word I/O publication of result `0037h` and exit code `600Dh`.

Sources:

- `firmware/workloads/paced_general.asm`
- `firmware/runtime/paced_bus_engine.c`
- `tests/paced_runtime/paced_native_runtime.c`

## Memory and clock policy

```text
00000h-3FFFFh  256 KiB RP2350 Internal-SRAM-backed RAM
10000h          flat workload load address
3FFFEh          initial stack top
F0000h-FFFFFh  reset ROM window
FFFF0h          reset handoff
```

The PIO clock program consumes one M33 token per complete pulse. The high and low
pulse widths remain bounded; only the low interval between complete pulses may be
extended. No READY wait state, Host callback, USB operation, or filesystem access
occurs inside a partially completed pulse.

## Build identity

```text
paced_native_runtime.uf2
SHA-256 7478b2d09b2558ba10be28de6d79ee6daf9cc2bee2bbf88c69d0448beb367cbd

paced_general_workload.bin
SHA-256 4c7a1d012b7dd61884f933648d83e35594f2a1ae8ff4c7044a9291f2f90597d7
```

## Physical procedure

1. Build target `paced_native_runtime` from the canonical repository.
2. Request RP2350 UF2 bootloader mode.
3. Copy `paced_native_runtime.uf2` to the verified RP2350 boot volume.
4. Open the re-enumerated Pico USB CDC port with DTR asserted.
5. The firmware holds the processor in reset until CDC is attached.
6. The RP2350 performs the reset sequence, services bus cycles, captures the two
   I/O publications, returns the processor to `RESET=HIGH`, `CLK=LOW`, and releases
   the AD bus.

## Retained output

```text
RP86 software-paced general Internal-SRAM runtime
Physical clock policy = software-issued complete pulses; no READY dependency
RAM = 00000h-3FFFFh, workload = 10000h (261 bytes), stack = 3FFFEh
Proof = LOOP + taken branch + PUSH/POP + byte/word RAM + word OUT

[PACED NATIVE EXECUTION]
First reset fetch        = FFFF0 PASS
Serviced bus cycles      = 218
Memory reads / writes    = 183 / 33
I/O reads / writes       = 0 / 2
Native result            = 0037 (expected 0037) PASS
Native exit              = 600D (expected 600D) PASS
General control flow     = LOOP / branch / stack / RAM PASS
Unmapped/lane/pad faults = 0/0/0
Last cycle               = IOW @ 000E6
PACED NATIVE RUNTIME RESULT = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Acceptance

The test is accepted because:

- the first physical fetch was the architectural reset vector `FFFF0h`;
- the native result and exit code both matched;
- 33 physical memory writes prove writable data and stack behavior;
- both byte and word memory paths were exercised;
- no unmapped address, invalid lane, or driven-pad mismatch occurred;
- the RP2350 returned the bus to the electrical safe state.

## What this proves

This proves that fixed-high READY does not prevent general Internal-SRAM-backed
execution when the processor clock is software-paced between complete pulses.
It establishes PACED as a real second engine alongside the existing CONTINUOUS
PIO/DMA runtime.

## What this does not yet prove

This test does not claim:

- runtime switching between CONTINUOUS and PACED;
- Host-shell load/run/restart integration for the PACED engine;
- PACED interrupt or two-cycle INTA handling;
- processor stdio or filesystem services;
- External-PSRAM-backed execution;
- a specific Intel/NEC processor identity from this image alone.

Processor identity remains a separate native AAD-16 validation. The operator's
installed processor may be recorded with the session, but this workload itself
only depends on the common 8086-class instruction set.

## Next integration step

The two engines will meet at an explicit safe-point handshake:

```text
workload OUT engine-request
        -> STI
        -> HLT
RP2350 completes the current pulse and holds CLK low
        -> switch engine and prepare state
        -> INTR / two-cycle INTA
        -> native ISR / IRET
        -> resume after HLT
```

Engine selection may also be supplied as launch metadata. No implementation is
allowed to change clock policy in the middle of a pulse or active bus phase.
