# pi86-rp2350 AI Bridge Implementation Plan

## Purpose

This document turns the accepted target in
[`ai_bridge_architecture.md`](ai_bridge_architecture.md) into independently
reversible implementation gates. It is intentionally separate from the design
note: the design note defines the destination; this file records how the
repository approaches it.

The canonical exchange remains:

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

## Stable contracts

### Host/Core message record

Core1/Core0 and the future USB transport use one fixed 64-byte, versioned,
provider-neutral record. The C definition is
`firmware/ai_bridge/bridge_protocol.h`; the Python definition is
`tools/ai_bridge/protocol.py`.

### V30 mailbox I/O map

| Port | Direction | Meaning |
|---|---|---|
| `00E0h` | V30 read | mailbox status |
| `00E2h` | V30 write | V30-to-host transmit data (byte or packed word) |
| `00E4h` | V30 read | host-to-V30 receive byte |
| `00E6h` | V30 read/write | control and message commit |

Byte cycles on these even ports use the low lane. A word write to `00E2h`
publishes two consecutive payload bytes, low byte first. `00E8h`, `00E9h`, and
`00EAh` retain their established regression/diagnostic meanings.

READY is fixed high on the present HAT, so Core0 must stage a complete inbound
message before exposing it to the V30. PIO/DMA, not an M33 current-cycle
lookup, owns the response deadline.

## Gates

### Python protocol gate

Run before attaching Codex or USB HID:

```sh
python3 -m unittest tests/ai_bridge/test_protocol.py
python3 tools/ai_bridge/v30bridge.py --simulate
```

This verifies byte layout, bounds, sequence preservation, and the canonical
conversation without claiming physical transport.

### AI-B0: physical scripted mailbox greeting

Status: **ACCEPTED on physical hardware (2026-08-23)**

Acceptance target: `ai_bridge_mailbox_200khz`

Timing-characterization target: `ai_bridge_mailbox` (0.300 MHz)

The RP2350 internal-SRAM response table contains the canonical greeting. The
physical V30 consumes and validates all greeting words, emits
`HELLO OPENAI CODEX` through `00E2h`, commits through `00E6h`, and enters a
checkpoint loop. The existing PIO/DMA current-address responder owns AD and
PINDIRS; the M33 is absent from current-cycle service.

AI-B0 is deliberately not the final asynchronous mailbox. Physical evidence at
0.300 MHz established a response-drive limit near the 31st linear-search entry,
while all earlier retained ROM responses remained coherent. The 0.200 MHz
acceptance build lengthens the physical T1 window without changing input
synchronizers, table contents, or bus ownership. Acceptance required physical
`AI-B0 RESULT = PASS` output with terminal high-Z safety; the run below
satisfies that gate.

The accepted 0.200 MHz run returned `HELLO OPENAI CODEX`, completed 77
supported reads with zero mismatches, observed the `00E2h` payload and `00E6h`
commit, and reached four checkpoint reads. See
[`validation/ai_b0_physical_mailbox_validation.md`](validation/ai_b0_physical_mailbox_validation.md).

### AI-B1: runtime-staged dual-core mailbox

Core1 accepts a complete 64-byte record and transfers ownership through a
bounded SPSC queue. Core0 stages the complete payload while the V30-visible
mailbox is not ready, then atomically publishes status. V30 reads `00E4h` and
writes `00E2h`; PIO/DMA handle every active bus cycle. This gate replaces the
AI-B0 build-time greeting while preserving AI-B0 as a hardware regression.

### AI-B2: USB HID and Python Host Bridge

Add a binary HID transport for the same 64-byte record. Python owns device
discovery, timeout/retry behavior, transcript rendering, and semantic calls:

```text
submit_ai_message(message)
receive_v30_message()
query_bridge_status()
```

The Python bridge is the integration oracle before Codex is connected.

### AI-B3: Codex Adapter

Map Codex turns onto the provider-neutral Host Bridge API. Codex sends the
first greeting only after the hardware has booted and the V30 mailbox reports
ready. Completion requires the two-line canonical transcript plus retained
physical V30 bus evidence.

## Evidence rule

Build success, Python simulation, and physical validation are reported as
different facts. A gate is accepted only after its own required physical or
end-to-end evidence is committed under `docs/validation/`; earlier accepted
PC1-C and dual-core regressions remain unchanged.
