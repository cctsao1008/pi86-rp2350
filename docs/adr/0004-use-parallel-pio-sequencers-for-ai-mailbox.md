# ADR 0004: Use parallel PIO1 sequencers for the AI mailbox

- Status: Accepted for AI-B1 implementation
- Date: 2026-08-23

> **Terminology note:** “AI mailbox” is retained in this ADR title as the
> historical AI-B1 gate identity. ADR 0005 establishes **Companion Service
> Mailbox** as the canonical name for new V30-visible architecture.

## Context

AI-B0 proved a complete physical greeting through the real V30 mailbox at
0.200 MHz. Its bounded current-address responder scans a 42-entry table from
entry zero for every bus cycle. Physical characterization at 0.300 MHz found
the first response error near the 31st entry. That result bounds this selector;
it does not bound PIO-direct AD response or the V30 clock architecture.

The existing responder already occupies all 32 instruction words in PIO1. A
mailbox fast path therefore cannot simply be appended to that program. The
runtime mailbox also has a stronger requirement than the AI-B0 ROM image: one
complete message must be staged at runtime, and a mailbox response word must
advance only when the matching mailbox read actually occurs. Unrelated ROM
fetches and I/O writes must not consume staged receive data.

RP2350 PIO state machines in one block share instruction memory but retain
independent FIFOs, shift registers, scratch registers, and program counters.
PIO output selection gives a simultaneous write from the highest-numbered
state machine priority. That rule permits deliberate overlap only when exact
cycle qualification and explicit ownership evidence make the responders
mutually exclusive.

## Decision

AI-B1 will not extend the 42-entry linear selector. It will use two independent
exact-key sequencer pairs in PIO1, sharing one compact program pair:

```text
PIO1 SM0  bounded ROM matcher      -- relative IRQ0 --> SM1
PIO1 SM1  bounded ROM responder                       AD/PINDIRS
PIO1 SM2  fixed mailbox matcher    -- relative IRQ2 --> SM3
PIO1 SM3  fixed mailbox responder                     AD/PINDIRS
```

The matcher and responder programs must fit together in PIO1's 32-word
instruction memory. Relative IRQ addressing allows both pairs to execute the
same instructions without sharing an authorization flag. Each pair receives
its own DMA-fed key and descriptor streams.

PIO1 SM3 has the hardware output priority, but memory-read and mailbox-I/O
keys must be exactly disjoint. A test is invalid if both responder pairs can
authorize AD ownership for one physical cycle. Only exact qualified hits may
change PINDIRS; misses remain high-Z and consume no response descriptor.

The runtime ownership path is:

```text
Core1 complete 64-byte record
        |
        v
bounded SPSC ownership transfer
        |
        v
Core0 immutable mailbox staging
        |
        v
DMA -> PIO1 mailbox key/descriptor FIFOs
        |
        v
physical V30 mailbox cycles
```

Core0 publishes mailbox-ready state only after the complete record and every
descriptor required by the bounded gate are locally staged. Neither M33 core
is allowed to resolve the current physical response cycle. A mailbox response
descriptor advances only after its exact mailbox key matches.

PIO0 remains the passive address/data observer. PIO2 owns the V30 clock so all
four PIO1 state machines remain available to the response plane. AI-B0 remains
unchanged as the permanent linear-selector hardware regression.

## Initial bounded gate

AI-B1-A is a bounded runtime-staging gate, not yet the final asynchronous
mailbox. It must prove:

1. Core1 transfers one complete provider-neutral record to Core0.
2. Core0 publishes no partial record.
3. ROM fetches consume only the ROM descriptor stream.
4. Qualified mailbox reads consume only the mailbox descriptor stream.
5. V30 mailbox writes and commit are retained by the passive observer.
6. No M33 current-cycle lookup occurs.
7. No dual-responder authorization or unqualified PINDIRS drive occurs.
8. RESET-high, CLK-low, AD-high-Z terminal safety remains intact.

The accepted AI-B0 0.200 MHz image remains a historical linear-selector
regression; it is not an AI-B1 performance target. AI-B1 first brings up the
new topology directly at 0.600 MHz, twice the 0.300 MHz original-system
baseline. The implementation target is:

```text
ai_bridge_runtime_mailbox_600khz   minimum AI-B1 acceptance target
```

A 0.300 MHz build is added only if a physical 0.600 MHz failure needs a
same-code timing diagnostic. It is not a planned acceptance stage.

## Consequences

The mailbox path becomes bounded by its fixed sequencer rather than by total
ROM table depth. Runtime message words are not skipped by unrelated bus
cycles. The four PIO1 state machines are committed during this gate, and the
ROM side is a known-path sequencer rather than a final arbitrary-address ROM.

A later ROM optimization may replace the known-path side with region/bank
decode, provided the mailbox ownership contract remains unchanged. General
dynamic ROM, RAM, PSRAM, and peripheral service still requires the controllable
READY capability planned for HAT v3.0.
