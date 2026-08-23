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

This is a validated host-adapter exchange, not a V30-visible AI abstraction.
The physical V30 sees only the companion mailbox and native I/O operations.
Codex, ChatGPT, a conventional test program, or another client remains above
the provider-neutral Host Bridge boundary. The stable architecture is defined
in [`ai_bridge_architecture.md`](ai_bridge_architecture.md).

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
| `00E6h` | V30 write | V30 reply commit |
| `00E8h` | V30 write | input-consumption or computation witness |

Byte cycles on these even ports use the low lane. A word write to `00E2h`
publishes two consecutive payload bytes, low byte first. `00E8h`, `00E9h`, and
`00EAh` may retain separate regression or diagnostic meanings in older tests;
each validation record states the exact ABI it exercised.

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

### Windows physical evidence gate

The synchronized Windows clone owns USB CDC capture and host-side physical
acceptance. `tools/ai_bridge/physical_validator.py` supports explicit COM-port
capture, lossless raw-log retention, offline revalidation, named acceptance
profiles, and automation-safe exit status. WSL remains the firmware build host.

```powershell
py tools\ai_bridge\physical_validator.py --list-ports
py tools\ai_bridge\physical_validator.py --port COM7 --profile ai-b1-a
```

The complete workflow and exit-code contract are defined in
[`development/windows_physical_validation.md`](development/windows_physical_validation.md).

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

AI-B1 replaces linear mailbox lookup with the parallel PIO1 sequencer topology
accepted in [ADR 0004](adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md).
The accepted AI-B0 targets and evidence remain unchanged.

#### AI-B1-A: bounded runtime staging

Status: **ACCEPTED ON PHYSICAL HARDWARE AT 0.600 MHz (2026-08-23)**

Build target: `ai_bridge_runtime_mailbox_600khz`

Evidence: [`validation/ai_b1a_runtime_mailbox_600khz_validation.md`](validation/ai_b1a_runtime_mailbox_600khz_validation.md)

Core1 accepts one complete 64-byte record and transfers ownership through a
bounded SPSC queue. Core0 copies the complete record into immutable local
staging before it enables either mailbox DMA stream. Partial messages are
never visible to the V30.

PIO1 uses two relative-IRQ matcher/responder pairs. The ROM pair and mailbox
pair share one compact program image but keep independent FIFOs and DMA
streams. An exact `00E4h` read advances one staged mailbox response; unrelated
ROM and I/O cycles consume none. PIO0 remains the passive witness and PIO2 owns
CLK. Acceptance requires explicit proof that the two responder pairs never
authorize the same cycle.

The retained 0.200 MHz AI-B0 image is a historical regression only. The
minimum and current AI-B1-A target is 0.600 MHz. A 0.300 MHz variant is created
only when needed to diagnose a physical 0.600 MHz failure; it is not an
acceptance target. Higher-frequency sweep work remains outside this gate.

The accepted physical run completed all 48 ROM pairs and all seven mailbox
pairs with zero deadline misses. Native V30 code read and XOR-validated
`HELLO NEC V30`, returned `HELLO OPENAI CODEX`, committed through `00E6h`, and
ended in the terminal safe state. This proves the bounded runtime-staging data
plane; live publication while the V30 is already polling remains AI-B1-B.

#### AI-B1-B: live mailbox publication

Status: **IMPLEMENTED LOCALLY; PHYSICAL 0.600 MHz EVIDENCE PENDING**

Build target: `ai_bridge_live_mailbox_600khz`

Windows sends one complete provider-neutral 64-byte binary record over CDC.
Core1 receives the entire record and transfers it through the SPSC ownership
boundary; Core0 validates it and prepares immutable key/response streams before
the deterministic V30 epoch. No partial record becomes V30-visible.

The already-running V30 first reads STATUS `00E0h` and must physically observe
`0000h` (NOT_READY). The mailbox PIO matcher sets a dedicated relative witness
IRQ only after authorizing that completed status cycle. Core0 polls that witness
with interrupts masked, then atomically starts both preconfigured mailbox DMA
channels. A bounded forward-only Native V30 path reads STATUS again and must
observe `0001h` (READY), consumes all seven `00E4h` words, emits a zero XOR
witness, returns `HELLO OPENAI CODEX`, and commits through `00E6h`.

The USB record is accepted before RESET release because Pico SDK USB IRQs remain
on Core0 and are masked during the V30 epoch. AI-B1-B proves Windows binary
ingress plus live post-NOT_READY publication; it does not claim concurrent USB
service during a physical bus cycle. That transport policy remains AI-B2.

Physical acceptance requires the Windows `ai-b1-b` profile to prove `0 -> 1`
STATUS ordering, simultaneous deferred-DMA publication, 121/121 ROM pairs, 9/9
mailbox pairs, zero deadline misses, the exact 230-byte ROM identity, and the
terminal RESET-high/CLK-low/AD-high-Z state.

On RP2350 an untriggered `CHx_TRANS_COUNT` exposes the zero live counter; the
programmed deferred count is the separate RELOAD value reported by
`CHx_DBG_TCR`. The clean pre-release gate therefore requires live counts `0/0`,
reload counts `8/8`, idle channels, and one prearmed NOT_READY word in each PIO
FIFO. Treating the zero live count as an unarmed DMA is incorrect.

#### AI-B1-C: sustained message exchange

Repeat complete records in both directions, validate sequence and ownership
transfer across Core1/Core0, and define bounded full/empty behavior. Human CDC
formatting remains on the service side and cannot stall the response plane.

AI-B1-A now replaces AI-B0 as the accepted bounded runtime-staging gate. AI-B0
remains a permanent linear-selector and electrical regression; AI-B1-B/C still
require their own physical evidence.

### AI-B2: USB HID and Python Host Bridge

The `ai_bridge_hid_mailbox_600khz` target adds CDC+HID composite transport
without changing AI-B1-B's V30/PIO/DMA response plane. HID owns the same fixed
64-byte request/reply ABI; CDC is receive-only physical evidence. Python owns
device discovery, timeout behavior, exact record validation, raw evidence,
deterministic explanation, and a stable JSON result for Codex.

AI-B2-HID is accepted by Windows physical validation at 0.600 MHz. One run
completed a 64-byte HID OUT request and matching 64-byte HID IN reply while
receive-only CDC evidence passed 38/38 deterministic checks, zero deadline
misses, and terminal bus safety. The exact raw log and JSON result are retained
under `docs/validation/evidence/`.

After composite acceptance, the next two message-level proofs are:

1. a fresh host challenge processed by native V30 CRC16 code and returned over
   HID, proving the answer cannot be a prerecorded string;
2. a 10-round sequence-checked development smoke test with loss, ordering,
   deadline, and bus-safety accounting. Larger soak counts are deliberately
   outside the current milestone.

The provider-neutral host API remains:

```text
submit_ai_message(message)
receive_v30_message()
query_bridge_status()
```

The Python bridge is the integration oracle and a provider-neutral host
boundary. Codex is the first adapter target, not a transport requirement. A
ChatGPT app/MCP adapter, OpenAI API client, or another host may invoke the same
bridge without changing the V30-visible protocol or physical evidence rules.

### AI-B3: Codex Adapter

Map an AI host turn onto the provider-neutral Host Bridge API. The first
accepted adapter is Codex: Codex sends the first greeting only after the
hardware has booted and the V30 mailbox reports ready. Completion requires the
two-line canonical transcript, a valid Host Bridge JSON result, and retained
physical V30 bus evidence. A ChatGPT adapter is an equivalent later endpoint,
not a different V30 transport.

AI-B3 is accepted. On 2026-08-23, Codex directly invoked the Windows bridge,
sent the canonical 64-byte HID greeting, received the physical V30's matching
64-byte reply as structured JSON, and retained a simultaneous CDC transcript.
All 38 deterministic checks and terminal bus safety passed. ChatGPT app/MCP
integration remains an optional second adapter rather than a prerequisite for
the accepted Codex-first goal.

## Evidence rule

Build success, Python simulation, and physical validation are reported as
different facts. A gate is accepted only after its own required physical or
end-to-end evidence is committed under `docs/validation/`; earlier accepted
PC1-C and dual-core regressions remain unchanged.
