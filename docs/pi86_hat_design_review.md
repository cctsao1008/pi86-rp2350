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

The canonical V3.0 target specification is
[`hardware/v3_companion_board_architecture.md`](hardware/v3_companion_board_architecture.md).
This review supplies evidence and rationale; it is not a schematic release or
manufacturing specification.

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
only connector for the robust V3.0 interface. A future design needs at least
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

## V3.0 hardware strategy

### Preserve the current board

The current physically validated HAT should not be reworked merely to explore a
new architecture. Label and retain it as the golden reference together with its
CPU. Experimental modifications should use a second HAT, an interposer, or a
new PCB.

### One consolidated hardware revision

The project will not introduce separate V1.2 and V2.0 intermediate HATs. The
next hardware design is **V3.0**, combining the previously proposed diagnostic,
wait-state, buffering, voltage-domain, and fail-safe improvements in one board.

V3.0 must include:

- disconnectable `READY` strap, defaulting to the legacy high state;
- open-drain/open-collector `READY` control with a defined pull-up and safe
  power-on default;
- selectable series damping footprints on CLK and the AD/control groups;
- test points for CLK, RESET, ASTB, READY, I/O-M, BUFR/W, UBE, AD ownership,
  3.3 V, and ground;
- explicit local V30 decoupling;
- current measurement provision for the CPU rail;
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

Legacy behavior must remain a deliberate jumper/configuration mode, but V3.0
is an active companion-chip interface rather than a passive pin adapter.

No 5 V CPU output may be connected directly to an RP2350 GPIO. Exact translator
families, direction logic, propagation delay, and READY timing require a
separate schematic-level timing and electrical review before layout.

## V3.0 connector plan

### J1: locked Raspberry Pi 40-pin legacy data plane

J1 preserves the existing physical-pin ABI so the current firmware mapping and
the golden HAT remain valid references. No bus signal is reassigned.

| Signal group | Raspberry Pi physical pins | Direction at RP2350 |
|---|---|---|
| AD0-AD15 | 37, 35, 33, 31, 29, 27, 23, 21, 19, 15, 13, 11, 7, 5, 3, 8 | Bidirectional |
| A16-A19 | 10, 12, 16, 18 | Input |
| ASTB, I/O-M, BUFR/W, UBE, INTAK | 32, 24, 26, 22, 28 | Input |
| CLK, RESET, INTR | 40, 36, 38 | Output |

These are all 28 GPIO-bearing header positions. The remaining header positions
are power and ground. Physical pins 27 and 28 are bus signals in Pi86, so the
interface is Raspberry Pi 40-pin mechanically compatible but does not reserve
the standard HAT ID-EEPROM pair.

J1 is the high-bandwidth V30 data plane:

```text
V30 address/data/control
        <-> V3.0 protection and transceivers
        <-> J1 legacy 40-pin mapping
        <-> RP2350 PIO/DMA
```

### J2: V3.0 auxiliary real-time control plane

Because J1 has no spare GPIO, V3.0 adds a keyed auxiliary connector. The
baseline allocation is:

| J2 pin | Signal | Direction | Timing role |
|---:|---|---|---|
| 1 | GND | - | Signal return |
| 2 | VIO_3V3 | Host -> HAT | Logic reference; not CPU power |
| 3 | `RESP_EN` | RP2350 -> HAT | PIO-timed authorization to drive V30 AD |
| 4 | `READY_REQ_N` | RP2350 -> HAT | PIO-timed open-drain wait request |
| 5 | `BUS_ARM` | RP2350 -> HAT | Slow global enable; defaults disabled |
| 6 | `BUS_FAULT_N` | HAT -> RP2350 | Sticky contention/illegal-state indication |
| 7 | `DBG_PHASE` | RP2350 -> HAT | Optional timing correlation/test output |
| 8 | GND | - | Signal return |

`RESP_EN` and `READY_REQ_N` are hard real-time signals and must be assigned to
PIO-capable RP2350 GPIO. `BUS_ARM` and `BUS_FAULT_N` are safety signals and must
not be omitted merely to save pins. `DBG_PHASE` may remain unpopulated if the
host carrier cannot provide a fifth auxiliary GPIO.

The exact RP2350 GPIO numbers for J2 are intentionally not locked yet. They
require a board-resource conflict review covering PIO-USB, MicroSD, DVI, PSRAM,
and connector accessibility. Native USB CDC and PSRAM should be preserved; an
unused PIO-USB pair is the first candidate for the two hard real-time signals,
subject to schematic and physical-pad verification.

### Local V3.0 safety logic

V3.0 must not depend on firmware alone to prevent contention. Local logic must:

- disable all V30-facing host drivers at power-on and while RESET is asserted;
- accept `RESP_EN` only during a qualified read-data phase;
- derive transceiver direction from the bus phase and read/write controls;
- make an illegal ownership combination force high-Z and latch
  `BUS_FAULT_N`;
- let `READY_REQ_N` extend a normal bus cycle without changing CLK semantics;
- provide a legacy mode with READY high and the buffered interface transparent.

This keeps the 40-pin interface focused on the V30 bus while J2 carries the
extra safety and wait-state semantics that do not fit on it.

### CPU option

A fully static V30H-class device could make clock stopping a legitimate board
capability, but availability and authenticity are practical risks. It would not
remove the value of `READY`: wait states preserve normal bus-cycle semantics and
support deterministic integration with memory and peripherals.

## Recommended decision

1. Keep the present HAT and installed CPU as the golden PC1 validation setup.
2. Continue current firmware work under the deterministic-hit/high-Z-miss rule.
3. Finish the general address-indexed SRAM responder so V3.0 timing and
   wait-state requirements are based on measured deadlines.
4. Freeze the J2 RP2350 GPIO allocation only after the complete host-board
   resource conflict matrix is reviewed.
5. Require separate schematic, timing, voltage-domain, and fail-safe reviews
   before manufacturing V3.0.

This sequence preserves the working evidence while allowing the hardware to
evolve from a Pi86 adapter into a defensible V30 companion-chip platform.
