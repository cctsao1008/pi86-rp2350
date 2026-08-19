# ADR 0003: Require READY or Deterministic Hits for General Memory

- Status: Accepted
- Date: 2026-08-19

## Context

PC1-C0C0 proved that the installed NEC V30 can execute a bounded,
address-qualified descriptor stream at 0.300 MHz. The current-cycle path is
entirely in PIO1, with SRAM tables feeding the matcher and responder through
DMA. PIO0 and a third DMA channel independently captured the physical address
and R2 data evidence.

That result does not provide a miss policy for a general address-indexed ROM,
RAM, or external PSRAM backend. The current Pi86 HAT connects V30 pin 22
`READY` directly to 3.3 V, so firmware cannot request a normal V30 wait state.

The installed CPU is a standard `D70116C-8` / `uPD70116C-8`, not a V30H.
The NEC data sheet specifies:

- `READY` inactive low requests a wait state;
- active-high `READY` during T3 or Tw allows the bus cycle to complete;
- READY must meet CLK-relative setup and hold timing because it is not
  synchronized internally;
- `uPD70116-8` clock cycle `tCYK` is 125 ns minimum and 500 ns maximum;
- clock-high width is at least 44 ns and clock-low width at least 60 ns.

NEC separately identifies the H-series/V30H as fully static and able to operate
from DC to its rated maximum frequency. That statement does not apply to the
installed standard C-8.

The project has repeatedly operated this particular C-8 at 0.300 MHz, whose
3.333 us period is already beyond the published 500 ns maximum. This is useful
empirical evidence for this physical sample, but it is not a documented timing
guarantee and cannot justify arbitrary active-cycle clock stopping.

The cited AC table is specified at nominal 5 V, while the current HAT operates
the nominally 5 V-rated C-8 at 3.3 V. NEC provides no applicable AC envelope
for this exact voltage/part combination. Successful project measurements must
therefore remain explicitly sample- and board-specific.

Primary references:

- NEC `uPD70116 (V30) 16-Bit Microprocessor: High-Performance, CMOS`, document
  50029-1 (NECEL-062), pin functions and AC/timing pages 3.66, 3.70, and 3.72;
- current HAT schematic and PCB prints, corroborated by `docs/hardware.md`;
- PC1-C0C0 physical record in
  `docs/validation/pc1c0c_descriptor_fed_sram_rom_validation.md`.

## Decision

The current standard-C-8 HAT must not use clock stretching or clock stopping
as the correctness mechanism for a general memory-service miss.

The consolidated V3.0 HAT shall expose a deterministic, PIO-controllable
`READY` path. This is a required capability, not an optional optimization: it
provides the bounded wait-state contract for dynamic ROM/RAM/peripheral misses
that cannot be guaranteed as fixed-latency on-chip hits.

Every CPU-visible service on the current HAT must follow one of these policies:

1. **Deterministic hit:** the response is guaranteed to be selected and driven
   from on-chip state before the fixed no-wait deadline.
2. **Explicitly unsupported cycle:** AD remains high-Z and the test reports the
   miss; firmware must not silently return stale or unrelated data.
3. **Hardware wait-state path:** a future HAT revision or documented rework
   exposes READY to deterministic logic which meets the NEC setup/hold rules.

External PSRAM is backing storage, not a direct current-cycle responder. It may
refill an internal SRAM cache outside the V30 deadline. A cache design is valid
only if the supported address set has a provable hit guarantee or the hardware
can assert READY on a miss.

No active-cycle clock-stretch experiment is required to accept this policy:
the installed part's published maximum clock period already excludes it as a
guaranteed mode. A later experiment may characterize empirical margin, but it
cannot promote clock stretching into the architectural miss policy.

## Prototype requirements

Before claiming general address-indexed SRAM ROM service, a prototype must:

- choose data from the current physical address, not an expected transaction
  sequence;
- support taken branches and repeated fetches without path prestaging;
- define a bounded worst-case response latency for every supported hit;
- keep unsupported or miss cycles high-Z and observable;
- prove that no DMA, FIFO, cache, or arbitration condition can substitute the
  wrong word;
- retain passive PIO0/DMA evidence and terminal RESET-high, CLK-low, AD-high-Z;
- begin at 0.300 MHz with default input synchronizers, while describing that
  clock point as empirical operation outside the standard C-8 AC range.

## Consequences

Positive consequences:

- correctness no longer depends on an undocumented clock-stop assumption;
- internal SRAM and PIO timing remain the explicit real-time boundary;
- PSRAM latency is isolated from the V30 bus through cache/refill policy;
- a future READY-capable HAT has a clear electrical and firmware requirement.

Costs and limitations:

- the existing HAT cannot recover transparently from a current-cycle cache
  miss;
- a guaranteed-hit cache may support only a bounded working set;
- arbitrary slow-memory access requires hardware change or rework;
- replacing the CPU with a fully static V30H would permit clock stopping, but
  it is a different validated hardware configuration and does not by itself
  expose READY on the present PCB.

## Alternatives rejected

### Stretch or stop CLK on the installed C-8

Rejected as a correctness guarantee because `tCYK` has a published 500 ns
maximum and the installed device is not the fully static H-series.

### Let the M33 perform the current-cycle lookup

Rejected by PC1-C0B measurements: capture, lookup, encode, and FIFO submission
missed the no-wait response deadline even at the empirical 0.300 MHz point.

### Return a placeholder and repair the result later

Rejected because a CPU-visible wrong instruction or data word cannot be made
safe after consumption.

### Treat descriptor-fed PC1-C0C0 as general ROM

Rejected because its bounded key sequence intentionally ignores unexpected
control flow. It is execution evidence, not random-access service.
