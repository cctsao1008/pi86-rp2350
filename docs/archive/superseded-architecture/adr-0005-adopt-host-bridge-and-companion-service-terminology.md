# ADR 0005: Adopt Host Bridge and Companion Service Terminology

- Status: Accepted
- Date: 2026-08-23

## Context

The first physical mailbox work used the project name “AI Bridge” and gate names AI-B0 through AI-B3. OpenAI Codex was the first host participant to send a complete record and accept a native reply from the physical NEC V30.

That naming is useful for the validated experiment but ambiguous as a machine architecture. A NEC V30 predates modern AI and cannot expose or consume an AI concept. It understands only native instructions, I/O, memory, interrupts, and device protocols. Treating “AI” as V30-visible terminology would couple the physical ABI to one modern host policy and obscure the actual asynchronous boundary.

The accepted implementation is already provider-neutral below the host adapter. Its 64-byte record, USB transport, Core ownership transfer, PIO/DMA response, and V30 mailbox do not require Codex or any model provider.

## Decision

The project adopts these canonical terms:

| Boundary | Canonical term |
|---|---|
| Modern program-facing API | **Host Bridge** |
| V30-visible machine abstraction | **Companion Service** |
| Fixed V30 I/O transport | **Companion Service Mailbox** |
| Provider-specific modern integration | **Host-side adapter** |
| Optional model-backed integration | **AI adapter** |
| Host-to-device conceptual record | `HOST_TO_V30_RECORD` |
| Device-to-host conceptual record | `V30_TO_HOST_RECORD` |

The V30-visible contract must not contain provider names, prompts, model identities, or the assumption that an AI service is present.

Codex, ChatGPT, an OpenAI API client, a debugger, a file service, or a conventional test program may all use the Host Bridge. Host policy chooses how to satisfy a Companion Service request; that choice is invisible to native V30 software.

## Historical naming policy

Accepted historical identities remain unchanged:

- AI-B0, AI-B1, AI-B2, and AI-B3 gate names;
- build-target names already used by validation evidence;
- validation transcript text such as `HELLO OPENAI CODEX`;
- ADR 0004 title and filename.

Those names identify actual experiments and Git history. They are not the canonical vocabulary for new V30-visible interfaces.

ADR 0004 remains valid for its PIO sequencer and ownership decision. This ADR supersedes only its broad “AI mailbox” terminology with **Companion Service Mailbox** for new architecture and implementation work.

## Consequences

Positive consequences:

- the V30 ABI matches concepts the processor and its software can actually express;
- provider-specific behavior remains replaceable;
- conventional test tools can validate the same bridge without an AI session;
- future BIOS, monitor, serial, disk, and debugger services can share one machine abstraction;
- host or network latency remains clearly outside current-cycle bus timing.

Costs:

- historical AI-B names coexist with the new canonical terms;
- documents must distinguish experimental identity from architectural vocabulary;
- future source identifiers may need gradual migration when that can be done without breaking retained evidence.

## Validation rule

A new host or AI adapter is accepted only when the provider-neutral Host Bridge and physical V30 evidence pass independently. A visible conversational reply does not redefine the V30-visible ABI and does not replace bus-safety validation.

## Related documents

- [`../../companion_service_abi.md`](../../companion_service_abi.md)
- [`host_bridge_and_ai_client_architecture.md`](host_bridge_and_ai_client_architecture.md)
- [`../../adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md`](../../adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md)
