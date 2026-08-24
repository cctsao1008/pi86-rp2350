# Companion Service ABI

- Status: **Canonical for protocol version 1**
- Applies to: Host Bridge, RP2350 service/realtime handoff, AI-B1/B2/B3 mailbox validation
- Source definitions: `firmware/ai_bridge/bridge_protocol.h`, `tools/ai_bridge/protocol.py`

## Purpose

This document is the canonical human-readable contract for communication between a modern host and native software running on the physical NEC V30.

The V30-visible abstraction is a **Companion Service**, not an AI service. Codex was the first validated host adapter, but provider identity is not present in this ABI.

The ABI has two related layers:

1. a fixed 64-byte host/Core record;
2. a bounded V30 I/O mailbox used by the accepted physical implementation.

## Version 1 host/Core record

Every version 1 record is exactly 64 bytes. Multibyte fields are little-endian.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0` | 1 | `version` | protocol version; must be `1` |
| `1` | 1 | `type` | message type |
| `2` | 2 | `flags` | version-specific flags; currently zero unless explicitly defined |
| `4` | 4 | `sequence` | transaction identity |
| `8` | 2 | `length` | valid payload bytes, `0..52` |
| `10` | 2 | `status` | record status |
| `12` | 52 | `payload` | payload followed by zero padding |

Equivalent packed layout:

```text
<BBHIHH52s
```

The record size and field offsets are normative. The C structure has a static size assertion, and the Python implementation asserts the same packed size.

### Message types

| Value | Name | Meaning |
|---:|---|---|
| `01h` | `HELLO` | bounded greeting or discovery transaction |
| `02h` | `TEXT` | opaque text payload |
| `03h` | `ACK` | acknowledgement |
| `04h` | `COMMAND` | host request for native Companion Service work |
| `05h` | `RESULT` | completed native result for the same sequence |
| `06h` | `HEARTBEAT` | low-priority V30 liveness record; never an application result |
| `7Fh` | `ERROR` | error record |

Unknown types must not be interpreted as a known service. A future service either defines a new type under a compatible version contract or increments the protocol version.

### Status values

| Value | Name | Meaning |
|---:|---|---|
| `0000h` | `OK` | accepted or completed |
| `0001h` | `BAD_VERSION` | unsupported record version |
| `0002h` | `BAD_LENGTH` | invalid payload length |
| `0003h` | `BUSY` | receiver cannot accept the transaction now |
| `0004h` | `TIMEOUT` | the bounded completion deadline expired |
| `0005h` | `BAD_SEQUENCE` | stale, ambiguous, or otherwise invalid transaction identity |
| `0006h` | `SERVICE_UNAVAILABLE` | the requested native service is not available |

### Flags

| Mask | Name | Meaning |
|---:|---|---|
| `0001h` | `RETRY` | retry the same unresolved sequence or request replay of its cached complete result |

Unknown flag bits are reserved and must not silently change execution.

### Sequence rules

1. A new initiating record receives a nonambiguous sequence number.
2. Its reply carries the same sequence.
3. A live sequence is not reused.
4. A retry sets `RETRY` and reuses the original sequence.
5. A retry never executes the native command twice. An in-flight request stays
   in flight; a completed request replays its cached complete result.
6. A same-sequence record without `RETRY`, a retry outside the retained replay
   window, or an ambiguous sequence is rejected as `BAD_SEQUENCE`.
7. A timeout produces an explicit same-sequence `RESULT` or `ERROR` with
   `TIMEOUT`; it is never represented by a fabricated heartbeat.
8. Heartbeats use their own monotonic sequence space, are lower priority than
   command results and faults, and may be coalesced or dropped with accounting.
9. Sequence equality identifies a transaction but is not a cryptographic integrity check.

### Payload rules

- `length` is authoritative and must not exceed 52.
- Bytes after `length` are padding and must not be treated as payload.
- Text is a payload convention, not a transport requirement.
- Embedded NUL bytes are valid for non-text services.
- A payload requiring more than 52 bytes is not representable as one version 1 record.

## Integrity boundary

Version 1 has **no CRC or cryptographic integrity field**. Its current evidence comes from:

- exact 64-byte HID record transfer;
- version and length validation;
- sequence preservation;
- complete-record ownership transfer;
- immutable publication to the realtime plane;
- V30 consumption and computation witnesses;
- an independently captured CDC physical-validation record.

A future CRC challenge may place challenge data inside the payload without changing the record layout. A general multi-record integrity field, fragmentation header, authentication field, or larger payload requires an explicitly versioned protocol extension. Reserved or padding bytes must not be silently repurposed.

## Direction

Direction is determined by the transport endpoint and ownership state, not by a dedicated version 1 header field:

```text
HOST_TO_V30_RECORD
V30_TO_HOST_RECORD
```

These are conceptual architecture names, not additional packed fields.

## V30 companion mailbox

The accepted bounded physical mailbox uses ordinary V30 I/O cycles:

| Port | V30 operation | Canonical meaning |
|---:|---|---|
| `00E0h` | read | publication status |
| `00E2h` | write | V30-to-host payload word |
| `00E4h` | read | host-to-V30 payload word |
| `00E6h` | write | V30 reply commit |
| `00E8h` | write | input-consumption or computation witness |

Older regressions may assign diagnostic meanings to one of these ports. Each validation record remains authoritative for the exact test image it documents. New Companion Service implementations use this canonical map unless they explicitly define and version another ABI.

### Publication status

The accepted live-mailbox gate uses:

| Value | Meaning |
|---:|---|
| `0000h` | `NOT_READY` |
| `0001h` | `READY` |

The V30 observes status through separate, normally completed I/O reads. Host or AI latency never holds an active bus cycle open.

### Word ordering

The current mailbox transfers packed words in V30 little-endian order. A payload with an odd byte count uses only the valid low-order final byte as defined by the bounded native program and its validation record.

### Commit and witness

The witness at `00E8h` demonstrates that native V30 code consumed or computed over the inbound data. The commit at `00E6h` declares the outbound reply complete. Observing a payload write without its required commit is not an accepted reply.

## Ownership model

```text
host adapter
    -> Host Bridge
    -> RP2350 service role
    -> bounded ownership transfer
    -> RP2350 realtime immutable staging
    -> DMA / PIO qualified mailbox response
    -> physical V30
```

The return path reverses only after a complete V30 reply commit.

Rules:

1. one owner may mutate a record at a time;
2. incomplete host records are never V30-visible;
3. Core0/Core1 numbering is implementation-specific, but service/realtime roles are not;
4. full queues reject or drop according to explicit policy without blocking the realtime producer;
5. neither M33 core, USB, nor a host process answers the current physical V30 cycle;
6. unsupported cycles remain high-Z.

## Error and timeout model

Version 1 defines record errors but not a complete long-running service state machine. A richer Companion Service should distinguish at least:

```text
IDLE
BUSY
READY
TIMEOUT
HOST_OFFLINE
BAD_SEQUENCE
BAD_LENGTH
BAD_VERSION
SERVICE_UNAVAILABLE
```

Adding these states to a packed record or V30 status word requires an explicit compatible definition or protocol version change.

## Persistent runtime lifecycle

The bounded v1 request/reply record remains unchanged. The accepted persistent
runtime adds lifecycle semantics around it without changing its layout.

The V30-visible model is:

```text
BOOT -> READY/IDLE -> BUSY -> DONE -> READY/IDLE
                         |
                         +-> FAULT
```

- V30-originated service traffic enters through the accepted native software
  interrupt wrapper at `INT 60h`.
- Host-originated work is published atomically by the RP2350 and announced to
  the V30 through a physical interrupt and INTA-qualified vector.
- Host-originated heartbeat traffic wakes the idle V30 through physical INTR
  and two-cycle INTA. A PIT-compatible timer remains an optional runtime service.
- Idle native code uses `STI`/`HLT`; interrupt handlers acknowledge and enqueue
  bounded work but do not wait for the host.

Heartbeat is a liveness class, not a successful-result class. It has lower
priority than fault, result, and command acknowledgement traffic, may be
coalesced or dropped under explicit backpressure policy, and carries a dropped
count or equivalent diagnostic witness. A full heartbeat queue must not block
the realtime producer or a current V30 bus response.

Persistent operation does not replace the validation terminal state. Bounded
validation targets continue to end at `RESET=HIGH`, `CLK=LOW`, and AD high-Z.
A persistent target instead requires separate proof that its `STI`/`HLT` idle
state releases the AD bus and that timer and companion interrupts preserve bus
ownership and response deadlines.

## Versioning policy

A change requires a new protocol version when it changes any of the following:

- record size or field offsets;
- meaning of an existing non-reserved field;
- payload capacity;
- fragmentation or reassembly rules;
- integrity or authentication layout;
- direction or service-dispatch semantics that old implementations could misinterpret.

Adding a new message type or status may remain version 1 only when unknown values are safely rejected by existing endpoints and the new definition does not change the meaning of an existing value.

## Acceptance requirements

An accepted transaction must agree across:

- encoded and decoded host record;
- sequence, length, type, and status;
- service-to-realtime ownership transfer;
- atomic mailbox publication;
- PIO/DMA qualified-pair completion;
- V30 consumption witness and reply commit;
- host reply record;
- the declared lifecycle end state: either bounded RESET-high/CLK-low/AD-high-Z,
  or persistent `STI`/`HLT` idle with AD high-Z and interrupt service armed.

Application success and physical evidence are separate assertions.

## Related documents

- [`ai_bridge_architecture.md`](ai_bridge_architecture.md) - architectural boundary and transaction flows
- [`ai_bridge_implementation_plan.md`](archive/completed-plans/ai_bridge_implementation_plan.md) - implementation gates
- [`dual_core_partitioning.md`](dual_core_partitioning.md) - ownership transfer and core roles
- [`development/windows_physical_validation.md`](development/windows_physical_validation.md) - physical validator workflow
- [`validation/ai_b2_hid_composite_600khz_validation.md`](validation/ai_b2_hid_composite_600khz_validation.md) - accepted composite HID/CDC evidence
- [`validation/companion_runtime_1mhz_validation.md`](validation/companion_runtime_1mhz_validation.md) - accepted persistent INT/INTR/INTA heartbeat evidence
- [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) - current project terminology
- [`archive/superseded-architecture/adr-0005-adopt-host-bridge-and-companion-service-terminology.md`](archive/superseded-architecture/adr-0005-adopt-host-bridge-and-companion-service-terminology.md) - historical terminology decision
