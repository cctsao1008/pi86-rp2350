# ADR 0002: Adopt the V30 Companion-Chip Architecture

- Status: Accepted
- Date: 2026-08-17

## Context

The project began as a deterministic RP2350 re-architecture of Pi86. Early gates reproduced the original functional chain with a software-stepped clock and Cortex-M33/SIO bus service. Performance Characterization 1 then tested whether the physical V30 response path could move out of per-cycle software.

The first DMA design attempted to move PIO events into SIO GPIO registers. That architecture was invalid because SIO is processor-local and is not a valid DMA target on RP2350. PC1-B replaced it with:

```text
SRAM encoded GPIO bitmap
        |
        v
DMA -> PIO1 TX FIFO
        |
        v
PIO1 OUT pins, 28 + MOV PINDIRS
        |
        v
scattered V30 AD0-AD15 pins
```

Only the AD pins are function-muxed to PIO1. Other GPIOs inside the contiguous GPIO0-27 OUT window remain assigned to their own functions.

On physical NEC V30 hardware, the post-reset `EB FE` self-loop discriminator passed at every configured clock from 0.300 through 8.000 MHz. The M33 cores were not in the per-cycle data-response path, and input synchronizer bypass was not required.

## Decision

The project is defined as an **RP2350-based V30 companion chip**, not as a board-for-board or software-stack clone of Pi86.

The intended ownership model is:

- PIO/DMA form the deterministic V30 bus data plane.
- A real-time core supervises address/control decoding, cache/refill work, and exceptional cycle handling that cannot remain entirely in PIO/DMA.
- A service core owns storage, USB, display, keyboard, disk images, debugging, and other work outside the hard timing path.
- Internal SRAM is the first deterministic ROM/RAM backend; external PSRAM is introduced only after its latency contract is measured.
- PC compatibility is implemented incrementally from CPU-visible dependencies, beginning with ROM execution and a diagnostic monitor before broad BIOS/DOS scope.

Core numbers are not permanently assigned by this ADR. The real-time and service roles must be measured before Core 0/Core 1 placement is locked.

## Validation boundary

PC1-B proves a fixed, pre-staged PIO-direct response at up to 8.000 MHz. It does not prove arbitrary address-qualified ROM/RAM service at that clock.

PC1-C must close the loop:

```text
ALE/T1 address and control capture
        |
        v
memory-read qualification
        |
        v
address-to-ROM lookup
        |
        v
PIO1 AD response
        |
        v
V30 CPU-visible control-flow checkpoint
```

The original HAT ties V30 `READY` high. The current hardware therefore cannot insert wait states for a ROM-cache, PSRAM, or service miss. General 8 MHz memory-service claims are prohibited until the complete dynamic lookup path is validated.

## Consequences

Positive consequences:

- The proven PIO-direct timing engine becomes reusable chipset infrastructure.
- Slow services are explicitly separated from the bus-critical data plane.
- The roadmap can target monitor, BIOS, storage, display, and DOS without returning to Linux-style GPIO polling.
- Performance claims remain tied to precisely stated response classes.

Costs and risks:

- Address-dependent service is harder than a pre-staged FIFO stream.
- `READY` being fixed high may require prediction, deterministic caching, a lower operating point, or a future hardware change for cache misses.
- Shared SRAM and bus arbitration between PIO, DMA, and both cores require measurement and disciplined memory placement.
- Existing software-stepped gates remain regression evidence but are not automatically proof of continuous-clock integrated behavior.

## Near-term sequence

```text
PC1-C0  address-qualified far-jump ROM
PC1-C1  diagnostic I/O port / Mini BIOS signature
PC1-D   deterministic RAM read/write backend
PC1-E   ROM monitor
PC1-F   minimum BIOS services
PC1-G   boot-sector and DOS/CP/M-86 exploration
```

## Superseded assumptions

- DMA-to-SIO is not a viable RP2350 response architecture.
- Core0/SIO software polling is no longer the target high-performance bus design.
- Eight megahertz is no longer merely a speculative stretch objective for fixed responses; it is a validated PC1-B point.
- PC1-B does not authorize an unqualified statement that all memory and peripheral service already works at 8 MHz.

