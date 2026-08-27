# Pi86 V20/V30 HAT Design Review

## Purpose

This document reviews the original Homebrew8088 Pi86 V20/V30 HAT as the electrical and mechanical bridge between the physical NEC V30 and the Waveshare RP2350-PiZero.

The current HAT has already enabled successful physical V30 execution and remains the project's working hardware baseline. This review therefore focuses on what the board provides, what constraints it imposes, and how firmware must operate within those constraints.

A replacement companion board is **not part of the current project direction**. Earlier V3.0 concepts remain historical design exploration only.

## Source scope and revision caveat

This review uses:

- the physical 2021 Pi86 HAT and its observed behavior;
- supplied Pi86 V20/V30 schematic and PCB material;
- current upstream Homebrew8088 design evidence summarized in [`hardware.md`](../../hardware.md);
- NEC V30 documentation;
- measured pi86-rp2350 physical validation results.

The available design artifacts are not assumed to be revision-identical. Statements below distinguish physical project behavior from observations made from design files.

## Executive assessment

The Pi86 HAT is intentionally minimal: it exposes a physical V20/V30-class CPU to a Raspberry Pi-compatible 40-pin interface with very little intermediate logic.

That simplicity is useful for this project because signal identity remains transparent and RP2350 PIO can interact directly with the legacy bus. The tradeoff is that the HAT supplies little hardware assistance for wait states, buffering, isolation, or fail-safe ownership.

The resulting architectural rule is straightforward:

> **Keep the existing HAT as the physical baseline and solve timing/ownership in the deterministic RP2350 bus plane unless a demonstrated blocker proves that hardware must change.**

## What the HAT provides

### Physical CPU and direct bus access

The board exposes the multiplexed NEC V30 address/data bus and control signals directly through the Raspberry Pi 40-pin header. This directness makes the physical bus easy to observe and has enabled PIO-based capture, qualified response, interrupt acknowledgement, and native ROM execution.

### Stable mechanical boundary

The Raspberry Pi physical 40-pin header is treated as the cross-platform mechanical ABI. Canonical signal mapping remains defined in [`hardware_contract.md`](hardware_contract.md) and [`pin_mapping.md`](pin_mapping.md).

### Fully allocated realtime GPIO set

The current interface consumes all 28 GPIO-bearing positions on the standard 40-pin header:

| Signal group | GPIO count |
|---|---:|
| AD0-AD15 | 16 |
| A16-A19 | 4 |
| CLK, RESET, ASTB, I/O-M, BUFR/W, UBE, INTR, INTAK | 8 |
| **Total** | **28** |

There are therefore no spare realtime header GPIOs for an added `READY` line, independent bus-enable signal, direction feedback, or other new hard-timing controls without changing the interface.

That is an architectural constraint of the current hardware baseline, not by itself a reason to redesign the board.

## Important limitations

### `READY` is fixed high

The reviewed HAT ties V30 `READY` high. RP2350 cannot insert a wait state through the existing interface.

Consequences:

- every supported current-cycle response has a fixed deadline;
- slow backing resources cannot be assumed to respond directly;
- an unsupported or unavailable response must not drive stale data;
- general PSRAM/storage access belongs outside the unbounded current-cycle path;
- clock stopping is not used as a substitute for a normal wait-state contract on the installed `D70116C-8`.

The memory policy is defined in [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](../../adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

### CPU voltage is an empirical project condition

The installed `D70116C-8` is nominally a 5 V device, while the Pi86 HAT operates it from 3.3 V in this setup.

Successful execution is useful empirical evidence for this particular hardware combination, but it is not a claim that all V30 parts are specified for 3.3 V operation.

### No independent bus buffering or isolation

The reviewed design does not provide a separate transceiver/interlock layer between the CPU bus and host GPIO.

Consequences include:

- firmware/PIO direction mistakes can create contention;
- the host GPIO voltage domain constrains replacement hardware choices;
- there is no independent hardware interlock forcing high-Z after a firmware failure.

The project therefore treats explicit PIO ownership and safe high-Z behavior as part of the firmware architecture.

### Limited dedicated instrumentation

The board was not designed as a modern logic-analysis fixture. Critical signals are available, but probing is mainly through the CPU/header rather than a purpose-built debug connector.

This is acceptable for the current project unless measurement repeatability becomes a demonstrated blocker.

## Why the existing HAT remains useful

The current hardware has several advantages for the research goal:

- it keeps the NEC V30 unquestionably physical;
- the bus is direct and easy to reason about;
- there is little hidden logic between CPU and RP2350;
- it provides a stable comparison point across firmware experiments;
- it forces deterministic response design rather than hiding timing behind a more capable board.

For pi86-rp2350, those properties are valuable. The project is primarily exploring how far the RP2350 can act as a programmable companion chipset around the existing physical interface.

## Current hardware policy

The working policy is:

1. Keep the existing Pi86 HAT unchanged as the primary baseline.
2. Do not redesign the HAT merely to make a planned feature easier.
3. Treat fixed-`READY` timing as an explicit architectural constraint.
4. Use PIO/DMA and bounded on-chip state for current-cycle responses.
5. Use PSRAM, Flash, storage, host tooling, and AI outside the unbounded current-cycle path.
6. Reconsider hardware only when a specific, measured requirement cannot be satisfied safely or meaningfully on the existing interface.

This keeps hardware evolution demand-driven rather than roadmap-driven.

## Historical V3.0 design exploration

The repository contains earlier work exploring a buffered replacement board with controllable `READY`, bus transceivers, auxiliary control signals, and additional electrical safeguards.

That work is preserved as engineering history in
[`archive/hardware-concepts/v3_companion_board_architecture.md`](v3_companion_board_architecture.md).
It is **not the canonical hardware target and is not currently planned for
implementation**.

The document may still be useful if future measurements reveal a hard limitation that justifies revisiting the hardware boundary.

## Related documents

- [`hardware_contract.md`](hardware_contract.md) — canonical physical interface
- [`pin_mapping.md`](pin_mapping.md) — signal mapping
- [`architecture.md`](../../architecture.md) — current companion-chip architecture
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](../../adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) — current fixed-`READY` memory policy
- [`archive/hardware-concepts/v3_companion_board_architecture.md`](v3_companion_board_architecture.md) — superseded historical replacement-board concept
