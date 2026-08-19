# Pi86 V20/V30 HAT Design Review

## Purpose

This document reviews the original Homebrew8088 Pi86 V20/V30 HAT as the
electrical and mechanical bridge between the physical NEC V30 and the
Waveshare RP2350-PiZero.

The current HAT has already enabled successful physical validation through
PC1-C0C0. It is therefore retained as the project's **golden reference HAT**.
This review does not authorize destructive modification of that board. It
identifies the limits of the existing design and separates low-risk fixture
improvements from a future buffered companion-chip board.

## Source scope and revision caveat

This review uses:

- the physical 2021 HAT and its validated behavior;
- the supplied Pi86 V20/V30 schematic PDF;
- the supplied Pi86 V20/V30 PCB PDF;
- the current upstream Homebrew8088 KiCad routing evidence summarized in
  [`hardware.md`](hardware.md);
- the NEC V30 documentation and measured PC1-B/PC1-C results.

The supplied PCB view is marked 2022, while the physical board is marked 2021
and current upstream KiCad material contains later changes. These sources are
strong routing evidence, but they are not assumed to be revision-identical.
Statements below distinguish verified project behavior from design-file
observations.

## Executive assessment

The HAT is a good **minimal Pi86 adapter** and a poor **general-purpose modern
bus interface**. That is not a contradiction: simplicity was appropriate for
the original slow software-driven Pi86 model, and the same direct wiring made
RP2350 bring-up inexpensive and transparent.

Its main strengths are:

- a socketed real NEC V20/V30-class CPU;
- a compact Raspberry Pi 40-pin mechanical interface;
- direct exposure of the multiplexed address/data and control bus;
- very little circuitry that can obscure signal identity during bring-up;
- proven operation with RP2350 PIO-direct response through PC1-C0C0.

Its main limitation is that it is a **passive pin adapter**, not a complete bus
controller. The provided design material shows direct CPU-to-header signal
routing, `READY` fixed high, V30 `VCC` at 3.3 V, and no discrete bus
transceivers or direction/enable safety logic. The RP2350 firmware must
therefore provide correct ownership and fixed-deadline response without help
from the HAT.

## What the current HAT does well

### Minimal and observable

The direct connection keeps the bus electrically and logically easy to trace.
There is no latch, CPLD, level translator, or transceiver state to include when
diagnosing a wrong address or data word. This simplicity contributed directly
to finding the original physical-pin translation error and validating the
PIO-direct AD path.

### Compatible mechanical boundary

The Raspberry Pi physical 40-pin header provides a stable cross-platform
mechanical ABI. The RP2350 port can preserve the HAT while translating each
physical position to the correct RP2350 GPIO. The canonical mapping remains in
[`hardware_contract.md`](hardware_contract.md).

### The interface is fully allocated

The existing design consumes all 28 GPIO-bearing positions on the Raspberry Pi
40-pin header:

| Signal group | GPIO count |
|---|---:|
| AD0-AD15 | 16 |
| A16-A19 | 4 |
| CLK, RESET, ASTB, I/O-M, BUFR/W, UBE, INTR, INTAK | 8 |
| **Total** | **28** |

This is sufficient for the present direct PIO bus engine, including memory,
I/O, and interrupt-acknowledge signaling. It has **no spare real-time GPIO** for
controllable `READY`, external transceiver enable, direction feedback, or an
ownership-fault input while preserving every current signal.

The 40-pin header can remain the legacy bus ABI, but it is not sufficient as the
only connector for the robust Rev 2 interface. A future design needs at least
one of:

- a small auxiliary high-speed connector to additional RP2350 GPIO;
- a new carrier that routes extra RP2350 pins directly;
- external bus-control logic that receives a smaller encoded control interface.

An I/O expander is suitable for configuration and diagnostics, but not for a
current-cycle `READY` decision. Temporarily repurposing `INTAK` can support a
memory-only experiment, but is not a full-system hardware contract.

### Suitable for deterministic PIO/DMA experiments

At the validated 0.300 MHz baseline, the HAT allows PIO1 to drive the scattered
AD pins directly and atomically control their directions. PC1-B proved the
V30 consumed `EB FE`, and PC1-C0C0 proved descriptor-fed execution of a real
assembled SRAM ROM with memory writes and passive DMA observation.

### Low cost and reversible

The original board can remain unchanged while firmware, host board, and test
fixtures evolve. It should remain available as a known-good comparison point
for every future board revision.

## Design limitations

### `READY` is fixed high

V30 pin 22 is tied to 3.3 V in the reviewed design. The RP2350 cannot request a
wait state when a ROM/RAM entry is absent from deterministic on-chip state, when
PSRAM is late, or when a peripheral service requires more time.

Consequences:

- current-cycle service must meet a fixed hardware deadline;
- a miss must remain high-Z rather than return stale or speculative data;
- PSRAM cannot be treated as an unbounded direct current-cycle responder;
- clock stretching or stopping is not a correctness mechanism for the installed
  standard `D70116C-8`.

The resulting memory policy is defined in
[`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

### The CPU is used outside its nominal voltage specification

The installed `D70116C-8` is nominally a 5 V device, while the reviewed HAT
routes 3.3 V to V30 `VCC`. Successful operation is valuable empirical evidence
for this particular CPU and board, but it is not a portable electrical guarantee
for every V30 sample, temperature, frequency, or replacement part.

### No bus buffering or level isolation

The supplied schematic and PCB views do not show discrete AD/control-bus
transceivers, level translators, current-limiting series networks, or an
independent hardware output-enable interlock. The CPU and host GPIO therefore
share the bus directly.

Consequences:

- a firmware direction error can create direct contention;
- host replacement choices are constrained by GPIO voltage tolerance;
- signal integrity and edge damping cannot be tuned per bus group;
- there is no external fail-safe that forces the host side high-Z during reset,
  boot, or firmware failure.

The validated PIO ownership rules reduce operational risk, but do not replace
board-level electrical protection.

### Limited instrumentation

The minimal board does not provide a structured logic-analyzer header or
dedicated test points for the critical phase and ownership signals. Attaching
probes directly to the CPU or 40-pin header is workable, but less repeatable and
more mechanically risky.

### Limited power-integrity evidence

The supplied design views do not show local V30 decoupling components. The
physical board must be inspected before declaring them absent, but a new design
should explicitly place local high-frequency decoupling at the CPU and provide
accessible ground points for measurement.

## Improvement strategy

### Preserve the current board

The current physically validated HAT should not be reworked merely to explore a
new architecture. Label and retain it as the golden reference together with its
CPU. Experimental modifications should use a second HAT, an interposer, or a
new PCB.

### Rev 1.1: diagnostic interposer with an auxiliary control connection

A small reversible interposer can address the highest-value weaknesses without
redesigning the whole HAT. Controllable `READY` requires an auxiliary connection
to an RP2350 GPIO, because the legacy 40-pin interface has no spare pin:

- disconnectable `READY` strap, defaulting to the legacy high state;
- open-drain/open-collector `READY` control with a defined pull-up and safe
  power-on default;
- selectable series damping footprints on CLK and the AD/control groups;
- test points for CLK, RESET, ASTB, READY, I/O-M, BUFR/W, UBE, AD ownership,
  3.3 V, and ground;
- explicit local V30 decoupling;
- optional current measurement point for the CPU rail.

This revision should preserve the physical-header ABI and make legacy behavior
the default jumper configuration.

### Rev 2: buffered V30 companion-chip board

The robust long-term design should treat the HAT as an active bus interface:

- bidirectional 16-bit AD transceivers with explicit direction and output
  enable;
- appropriate unidirectional buffering or level translation for address/status,
  clock, reset, interrupt, and control signals;
- hardware default states that keep all host-driven bus outputs disabled until
  firmware deliberately arms the interface;
- an auxiliary host connection for `READY`, transceiver enables, and fault
  feedback rather than overloading the legacy 40-pin signal set;
- controllable `READY` for bounded wait-state insertion;
- protection against simultaneous CPU/host AD ownership;
- local decoupling, ground-rich debug headers, and short, grouped bus routing;
- optional selectable 3.3 V experimental and nominal-voltage CPU power domains,
  only with translators that protect the RP2350 from 5 V signals.

No 5 V CPU output may be connected directly to an RP2350 GPIO. Exact translator
families, direction logic, propagation delay, and READY timing require a
separate schematic-level timing and electrical review before layout.

### CPU option

A fully static V30H-class device could make clock stopping a legitimate board
capability, but availability and authenticity are practical risks. It would not
remove the value of `READY`: wait states preserve normal bus-cycle semantics and
support deterministic integration with memory and peripherals.

## Recommended decision

1. Keep the present HAT and installed CPU as the golden PC1 validation setup.
2. Continue current firmware work under the deterministic-hit/high-Z-miss rule.
3. Add an interposer or second-board Rev 1.1 before experiments that require
   `READY`, repeated probing, or signal damping.
4. Design Rev 2 only after the general address-indexed SRAM responder has
   established concrete latency, direction, and wait-state requirements.
5. Require separate schematic, timing, voltage-domain, and fail-safe reviews
   before manufacturing Rev 2.

This sequence preserves the working evidence while allowing the hardware to
evolve from a Pi86 adapter into a defensible V30 companion-chip platform.
