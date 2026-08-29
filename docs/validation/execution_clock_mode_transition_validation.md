# Execution Clock Mode Transition Validation

- Date: 2026-08-29
- Hardware: Waveshare RP2350-PiZero, unmodified Pi86 HAT, physical Intel P8086-2
- Processor clock: 1.000 MHz in FREE_RUNNING mode
- Firmware target: `execution_clock_runtime`
- Result: **PASS**

## Purpose

Prove that a native 8086-class workload can request an Execution Clock Mode
change and that RP2350 can move between CLOCK_STEPPED and FREE_RUNNING only at a
complete processor bus-cycle boundary with `CLK=LOW`.

## Native handshake

The 54-byte workload is loaded at physical `10000h`. It installs an `INT 60h`
handler, computes `19 + 23`, publishes `002Ah`, and invokes `INT 60h`. The handler
writes request value `0001h` to execution-clock control port `00EAh`.

RP2350 services that complete I/O write cycle under CLOCK_STEPPED. After the
cycle is committed and CLK is low, it enables FREE_RUNNING. The interrupt handler
returns under the new clock policy and the foreground reaches `STI; HLT`.

For the return transition, the FREE_RUNNING PIO program samples a stop token only
after completing the low half-cycle. The same state machine acknowledges the
stopped-low state through its RX FIFO and blocks; RP2350 then reinitializes it for
CLOCK_STEPPED operation. No shared PIO IRQ is consumed by this handshake.

## Retained physical output

```text
RP86 execution-clock transition runtime
Start = CLOCK_STEPPED; native INT 60h requests FREE_RUNNING
Return = RP2350 safe-stop acknowledgement at CLK=LOW

[EXECUTION CLOCK MODE TRANSITION]
First reset fetch       = FFFF0 PASS
Clock-stepped cycles    = 41
Native result           = 002A (expected 002A) PASS
INT 60h clock request   = 0001 PASS
Request cycle boundary = CLK=LOW PASS
FREE_RUNNING edges      = 3997 PASS
Safe return to stepped  = CLK=LOW PASS
Last observed cycle     = IOW @ 000EA
Unmapped/lane/pad faults= 0/0/0
EXECUTION CLOCK TRANSITION RESULT = PASS
Processor halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Acceptance

The transition is accepted because:

- the first native fetch was the architectural reset vector `FFFF0h`;
- the physical Intel 8086 produced the expected native result `002Ah`;
- `INT 60h` published exactly one mode request at `IOW @ 00EAh`;
- the request cycle ended with `CLK=LOW` and no unmapped, lane, or pad fault;
- FREE_RUNNING produced 3997 observed edges during the 2 ms sampling window;
- the controller safely returned to CLOCK_STEPPED with `CLK=LOW`;
- final recovery asserted RESET and released the multiplexed AD bus.

## Evidence boundary

This proves the RP2350 clock controllers and native request/commit handshake on
the installed Intel 8086. It does not yet expose clock-mode selection through the
canonical Host shell lifecycle, validate the same transition on every processor
sample, or make a vendor guarantee that every 8086-class device is fully static.
