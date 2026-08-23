# V3.0 Companion Board Architecture

- Status: **Target architecture; not released for schematic or manufacture**
- Legacy reference: original Pi86 V20/V30 HAT plus Waveshare RP2350-PiZero
- Processor target: physical NEC V30, with legacy-mode compatibility for the validated setup

## Purpose

V3.0 consolidates the experimentally validated `pi86-rp2350` companion-chip architecture into hardware designed for deterministic ownership, controllable wait states, voltage-domain protection, and repeatable measurement.

It is not merely a mechanical adapter. It is the intended physical boundary between:

```text
physical NEC V30 bus
        |
protection / translation / ownership interlock
        |
legacy 40-pin data plane + auxiliary realtime control
        |
RP2350 PIO / DMA / SRAM / service planes
```

This document defines board-level responsibilities and interfaces. It does not select final translator part numbers, PCB stack-up, routing rules, or production components.

## Design goals

V3.0 shall:

1. preserve the validated Raspberry Pi physical 40-pin signal mapping as a legacy data-plane ABI;
2. expose deterministic RP2350 control of V30 `READY`;
3. prevent AD-bus contention with hardware interlocks, not firmware convention alone;
4. isolate nominal V30 and RP2350 voltage domains;
5. provide explicit RESET, CLK, response-enable, and fault semantics;
6. retain passive observation independently of active response;
7. support current firmware in a deliberate legacy mode;
8. provide repeatable test points for timing and ownership evidence;
9. leave bulk memory and slow peripherals outside an unbounded current-cycle path;
10. preserve a safe power-on and fault state.

## Non-goals

The first V3.0 board need not:

- reproduce an IBM PC motherboard chipset electrically;
- expose every future peripheral on the first PCB revision;
- guarantee integrated 8 MHz operation before physical characterization;
- place PSRAM, MicroSD, USB, or AI services directly in the current V30 cycle;
- make a standard V30C fully static by stopping CLK;
- discard the original HAT as the golden regression reference.

## Compatibility modes

### Legacy mode

Legacy mode preserves the present behavior:

- J1 follows the original 40-pin physical mapping;
- READY is effectively high;
- buffering is transparent within its validated timing envelope;
- the existing deterministic-hit/high-Z-miss firmware model remains usable;
- auxiliary realtime controls default to inactive-safe states.

### Managed mode

Managed mode enables V3.0 features:

- `READY_REQ_N` can request normal V30 wait states;
- `RESP_EN` explicitly authorizes AD drive;
- `BUS_ARM` gates all active companion ownership;
- local logic rejects illegal direction or phase combinations;
- `BUS_FAULT_N` reports a sticky hardware ownership fault.

Mode selection must be explicit and observable. A floating strap or uninitialized RP2350 must not enter managed-drive mode.

## Top-level partition

```text
                  +----------------------+
                  |   physical NEC V30   |
                  +----------+-----------+
                             |
                 AD / address / control / CLK
                             |
        +--------------------+--------------------+
        | V3.0 V30-facing protection and buffers |
        | direction decode / ownership interlock |
        | READY logic / fault latch / test points |
        +---------------+-------------------------+
                        |
          +-------------+--------------+
          |                            |
   J1 legacy 40-pin data plane   J2 realtime control plane
          |                            |
          +-------------+--------------+
                        |
             RP2350-PiZero or carrier
             PIO / DMA / SRAM / USB
```

## J1 legacy data plane

J1 retains the Raspberry Pi physical 40-pin positions used by the original HAT. No bus signal is reassigned.

| Signal group | Raspberry Pi physical pins | RP2350-side direction |
|---|---|---|
| AD0-AD15 | 37, 35, 33, 31, 29, 27, 23, 21, 19, 15, 13, 11, 7, 5, 3, 8 | bidirectional |
| A16-A19 | 10, 12, 16, 18 | input |
| ASTB, I/O-M, BUFR/W, UBE, INTAK | 32, 24, 26, 22, 28 | input |
| CLK, RESET, INTR | 40, 36, 38 | output |

The physical-pin mapping is canonical in [`../hardware_contract.md`](../hardware_contract.md). This table is an architectural grouping, not a replacement for the full pin contract.

Physical pins 27 and 28 remain Pi86 bus signals rather than a standard HAT ID-EEPROM interface.

## J2 realtime control plane

J1 has no spare GPIO-bearing positions. V3.0 therefore requires a keyed auxiliary connector.

| J2 pin | Signal | Direction | Role |
|---:|---|---|---|
| 1 | GND | - | signal return |
| 2 | `VIO_3V3` | host to board | RP2350 logic reference, not V30 power |
| 3 | `RESP_EN` | RP2350 to board | PIO-timed authorization to drive AD |
| 4 | `READY_REQ_N` | RP2350 to board | deterministic wait-state request |
| 5 | `BUS_ARM` | RP2350 to board | slow global ownership enable |
| 6 | `BUS_FAULT_N` | board to RP2350 | sticky illegal-state indication |
| 7 | `DBG_PHASE` | RP2350 to board | optional timing-correlation output |
| 8 | GND | - | signal return |

The connector pin roles are architectural. Exact RP2350 GPIO allocation remains open until PIO-USB, native USB, MicroSD, DVI, PSRAM, and physical routing conflicts are reviewed together.

`RESP_EN` and `READY_REQ_N` require PIO-capable timing. `BUS_ARM` and `BUS_FAULT_N` are mandatory safety signals. `DBG_PHASE` may be unpopulated when carrier resources are insufficient.

## Voltage and power domains

The nominally 5 V-rated V30 and 3.3 V RP2350 must not be connected through unreviewed direct GPIO paths.

The schematic review shall define:

- V30 core and I/O supply intent;
- RP2350-side logic reference;
- translator input tolerance and output levels;
- power-up and power-down sequencing;
- back-power prevention;
- unpowered-device behavior;
- pull directions for RESET, READY, ownership, and fault signals;
- connector power-current limits;
- whether legacy 3.3 V V30 operation remains available only as an empirical mode.

No final translator family is selected by this architecture document. Propagation delay, direction control, output-enable timing, bus capacitance, and fail-safe behavior require schematic-level analysis.

## AD-bus ownership interlock

Local hardware shall compute whether V30-facing host drivers may enable. The safe default is disabled.

Drive authorization requires all applicable conditions:

```text
BUS_ARM asserted
RESET state permits the experiment
qualified V30 read-data phase
direction decoded as companion-to-V30
RESP_EN asserted in the allowed timing window
no latched fault
```

Any illegal combination forces high-Z and latches `BUS_FAULT_N`. Firmware may clear a fault only through an explicit recovery sequence after returning the CPU to a safe state.

The interlock must prevent both the V30 and companion drivers from owning AD simultaneously even if firmware, PIO configuration, or connector state is wrong.

## READY architecture

The present HAT ties READY high. V3.0 makes deterministic READY control mandatory.

Requirements:

- default and legacy mode present READY high;
- managed mode allows PIO-timed `READY_REQ_N` to request wait states;
- the board meets V30 CLK-relative setup and hold requirements;
- deassertion and release are deterministic across T3/Tw;
- a slow host, USB, filesystem, or AI service never directly controls READY;
- the realtime plane requests waits only from locally known state;
- timeout and fault policy remain defined if a response never becomes ready.

READY is the normal miss mechanism for dynamic ROM/RAM/peripheral service. CLK stopping is not a substitute for the installed standard V30C timing contract.

## CLK, RESET, and interrupt control

- CLK remains a PIO-owned deterministic output with a defined LOW stop boundary.
- RESET defaults asserted until power, configuration, bus ownership, and required local state are valid.
- INTR is driven only by the qualified interrupt-controller path.
- RESET must asynchronously or locally disable companion AD drivers.
- a watchdog or host disconnect may return the system to RESET-high, CLK-low, AD-high-Z without waiting for service software.

## Memory and peripheral boundary

Internal RP2350 SRAM remains the deterministic hot-state resource for:

- PIO/DMA queues;
- response descriptors;
- hot ROM/RAM lines;
- mailbox and device state;
- retained trace required for acceptance.

External PSRAM, flash, MicroSD, USB, display, and host services are bulk or asynchronous resources. They may refill or consume locally staged state outside the active cycle. A managed READY path may cover bounded misses, but it does not turn an unbounded service into a realtime responder.

## Debug and measurement

V3.0 should expose labeled, ground-referenced test points for at least:

- CLK;
- RESET;
- ASTB;
- READY;
- `RESP_EN`;
- representative AD bits or an accessible AD header;
- `BUS_ARM`;
- `BUS_FAULT_N`;
- `DBG_PHASE` when fitted;
- relevant supply rails.

Probe loading and test-point placement are part of timing review. A signal that cannot be measured repeatably is difficult to validate as a hardware contract.

## Safe-state contract

### Power-on

```text
BUS_ARM      inactive
RESP_EN      inactive
READY_REQ_N  inactive
V30 RESET    asserted
V30 CLK      low or defined reset clock sequence
AD drivers   high-Z
fault latch  observable
```

### Normal terminal state

```text
RESET = HIGH
CLK   = LOW
AD    = high-Z
```

### Fault response

An ownership, power-domain, or sequencing fault forces AD high-Z and prevents automatic V30 release. The fault remains observable after RESET assertion.

## Resource allocation review

Before locking J2 GPIO numbers, record every candidate conflict:

| Resource | Questions |
|---|---|
| Native USB | Does the design preserve the accepted CDC/HID path? |
| PIO-USB pins | Are they physically routed and available for realtime control? |
| MicroSD | Which GPIO, DMA, and interrupt resources are consumed? |
| DVI | Which PIO, DMA, clocks, and pins are reserved? |
| PSRAM | Which PIO/QMI/SPI resources and pins are required? |
| Debug | Are SWD, UART, and test outputs accessible? |
| PIO blocks | Can clock, response, observer, and future device engines coexist? |

This review freezes a complete system allocation, not one connector in isolation.

## Validation sequence

V3.0 shall be accepted in bounded stages:

1. unpowered continuity, shorts, orientation, and rail checks;
2. RP2350-side power and reset with the V30 held in reset;
3. translator truth-table and high-Z validation without the CPU;
4. legacy-mode replay of accepted PC1-B/PC1-C regressions;
5. hardware fault-injection proof that illegal ownership remains high-Z;
6. READY setup/hold and single-wait-state characterization;
7. bounded deterministic memory miss serviced through READY;
8. integrated ROM/RAM/I/O regression at conservative clock;
9. frequency characterization by evidence class;
10. sustained workload and peripheral integration.

Each stage introduces one primary unknown and retains the prior safe-state regression.

## Open decisions before schematic freeze

- final V30 supply and compatibility modes;
- translator and bus-switch topology;
- READY generation and synchronizing logic;
- fault latch implementation and reset policy;
- J2 RP2350 GPIO allocation;
- consolidated board versus adapter/carrier mechanics;
- fan and auxiliary power policy;
- test-point and logic-analyzer connector format;
- whether PSRAM belongs on the companion board or RP2350 carrier;
- manufacturing test strategy.

## Release criteria for schematic design

Architecture may advance to schematic review only when:

1. the pin and resource allocation matrix is complete;
2. voltage-domain requirements are explicit;
3. every bidirectional path has defined direction and output-enable truth tables;
4. READY timing has a calculable budget;
5. power-on, reset, disconnect, and fault states are safe without firmware cooperation;
6. legacy compatibility and managed mode are distinguishable;
7. test points support the acceptance plan;
8. no target performance claim exceeds the evidence class being designed.

## Related documents

- [`../pi86_hat_design_review.md`](../pi86_hat_design_review.md) - source review and rationale
- [`../hardware_contract.md`](../hardware_contract.md) - canonical J1 physical mapping
- [`../pin_mapping.md`](../pin_mapping.md) - detailed signal mapping
- [`../adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](../adr/0001-use-rpi-physical-pin-as-hardware-abi.md) - 40-pin ABI decision
- [`../adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](../adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - READY policy
- [`../architecture.md`](../architecture.md) - system timing and ownership architecture
