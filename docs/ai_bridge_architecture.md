# Host Bridge and AI Client Architecture

## Purpose

The Host Bridge is the provider-neutral boundary between the Host Runtime
Controller and native code running on the Bare-Metal Remote Physical Processor.

It is one part of the wider **Observe / Control / Experiment** interface. The bridge carries structured host requests, replies, configuration, and retained machine data without placing host or USB latency in the V30 current-cycle timing path.

```text
Host tools / AI
       |
Observe / Control / Experiment
       |
    Host Bridge
       |
 RP2350 services
       |
PIO / DMA bus plane
       |
 Physical NEC V30
```

The first validated implementation used fixed USB HID records, CDC evidence,
and an OpenAI Codex adapter. Those experiments remain validation evidence; they
do not define the project identity or make an AI provider part of the V30
runtime.

## Architectural boundary

To the physical V30, the RP2350 exposes ordinary machine mechanisms:

- I/O ports;
- memory and mailbox state;
- polling and interrupts;
- native code and CPU-visible device semantics.

To the Host, the bridge exposes structured records and runtime operations.

Provider-specific concepts such as prompts, credentials, sessions, model names, or service-specific response formats terminate above the Host Bridge boundary.

The V30 does not know what AI is. AI is simply one possible host-side client.

## Relationship to the realtime path

The Host Bridge is never part of an active V30 bus response cycle.

PIO and DMA own qualified current-cycle capture and response. Arm firmware prepares and supervises bounded state around that path. USB, host processes, storage, network access, and AI reasoning remain outside the current-cycle path as asynchronous services.

The rule is:

> **Host latency may delay a host-visible service, but it must not become a hidden dependency of current-cycle V30 timing.**

The existing Pi86 HAT holds `READY` high, so a host service cannot stretch an active V30 cycle while waiting for a reply.

## Observe / Control / Experiment

### Observe

The Host Bridge can expose structured machine data such as:

- bus activity and transaction summaries;
- memory/I/O and interrupt activity;
- response class and timing metadata;
- machine/device state;
- runtime and service state;
- retained trace or experiment results.

Raw retained data should remain distinguishable from decoded or AI-derived interpretation.

### Control

The Host Bridge can carry bounded operations such as:

- reset/run control;
- supported clock or mode configuration;
- ROM/test-image selection;
- trace filters and triggers;
- supported IRQ or companion requests;
- safe machine-state queries;
- explicitly permitted mailbox or test-memory operations.

These are host requests, not direct current-cycle GPIO actions. Firmware remains responsible for validating preconditions, ownership, and safe execution boundaries.

### Experiment

A host-side experiment can:

1. configure a bounded test;
2. execute it on the physical V30;
3. retain structured results and configuration identity;
4. compare runs or operating points;
5. provide the same data to scripts, test programs, or AI-assisted analysis.

AI may help form hypotheses, diagnose failures, compare results, or choose the next bounded experiment. It does not control the current-cycle bus path.

## Provider-neutral host interface

The Host Bridge must remain useful without AI.

Possible clients include:

- Python or native test tools;
- console and debugger front ends;
- trace-analysis software;
- image or workload managers;
- automated regression runners;
- AI adapters such as Codex, ChatGPT, or another model/service.

Provider-specific behavior stays in the client adapter. The RP2350 and V30-visible ABI should contain only stable machine concepts.

## V30-visible companion service

The V30 sees a companion service expressed through normal machine semantics, not an AI abstraction.

The currently validated mailbox implementation uses these I/O ports:

| Port | V30 operation | Meaning |
|---:|---|---|
| `00E0h` | read | status/publication state |
| `00E4h` | read | host-to-V30 payload word |
| `00E8h` | write | V30 consumption witness or computation result |
| `00E2h` | write | V30-to-host payload word |
| `00E6h` | write | V30 reply commit |

These ports describe an accepted bounded implementation. Future services may use different machine-facing wrappers while preserving the same architectural separation between realtime bus handling and asynchronous host work.

The exact record layout and versioning rules are defined in [`companion_service_abi.md`](companion_service_abi.md).

## Transport

The validated version 1 bridge used one fixed 64-byte USB HID record in each direction:

```text
version / type / flags / sequence / length / status / 52-byte payload
```

CDC was used as an independent engineering-output channel during validation.

This HID/CDC arrangement is a transport choice, not an architectural requirement. A future Host Bridge implementation may use another transport as long as it preserves complete-record publication, sequence identity, bounded ownership transfer, and separation from the realtime bus path.

## Ownership and publication

Mutable data has one owner at a time.

A typical host-to-V30 flow is:

```text
host client
    -> Host Bridge
    -> RP2350 service role
    -> immutable staging
    -> realtime publication
    -> V30 consumption
```

The return path reverses ownership only after the V30 commits a complete reply or result.

Cross-role transfer must use bounded queues or explicit ownership handoff. A full queue may reject or drop work according to policy, but it must not expose partial state or block a producer that participates in realtime supervision.

## Historical validated bridge path

The accepted AI-B experiments demonstrated one concrete bridge implementation:

- a fixed 64-byte HID request/reply record;
- sequence-preserving publication through RP2350 state;
- native V30 mailbox reads/writes and reply commit;
- receive-only CDC engineering output;
- a Codex adapter that initiated a greeting transaction.

The validated greeting was:

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

This proves a provider adapter could use the bridge. It is not the semantic definition of the Host Bridge and should not constrain future clients or experiments.

Historical validation records should retain their original terminology, measured values, and transport details.

## Failure behavior

Host-facing failures must remain explicit and bounded. Typical examples include:

```text
TIMEOUT
HOST_OFFLINE
BAD_SEQUENCE
BAD_LENGTH
SERVICE_UNAVAILABLE
QUEUE_FULL
```

A host or service failure may cause a request to fail, but it must not silently alter PIO/DMA ownership or create an unsafe V30 bus response.

## Design summary

The Host Bridge provides a stable translation layer between the Host-managed
runtime and a physical 8086-class processor:

```text
Physical V30
    |
PIO / DMA realtime bus plane
    |
RP2350 companion services
    |
Host Bridge
    |
Observe / Control / Experiment
    |
Tools / scripts / AI
```

The architectural distinction is simple: the Host controls the runtime, the
RP2350 owns companion resources and the physical bus, and the physical V30 owns
native execution.

## Related documents

- [`architecture.md`](architecture.md) - canonical Host-managed runtime architecture
- [`host_runtime_architecture.md`](host_runtime_architecture.md) - detailed runtime contract
- [`project_overview.md`](project_overview.md) - project identity and scope
- [`companion_service_abi.md`](companion_service_abi.md) - canonical host record and V30 mailbox ABI
- [`dual_core_partitioning.md`](dual_core_partitioning.md) - RP2350 realtime/service role partition
- [`ai_bridge_implementation_plan.md`](archive/completed-plans/ai_bridge_implementation_plan.md) - archived staged bridge implementation work
- [`development/windows_physical_validation.md`](development/windows_physical_validation.md) - Windows validation workflow
- [`validation/ai_b2_hid_composite_600khz_validation.md`](validation/ai_b2_hid_composite_600khz_validation.md) - accepted HID/CDC physical bridge evidence
- [`validation/ai_b3_codex_initiated_greeting_validation.md`](validation/ai_b3_codex_initiated_greeting_validation.md) - accepted Codex adapter evidence
