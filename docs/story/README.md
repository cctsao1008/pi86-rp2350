# The pi86-rp2350 Story

This four-part series explains the architectural milestones of
`pi86-rp2350` as a human-readable engineering story. The articles complement,
but do not replace, the canonical architecture contracts and physical
validation records.

The progression is:

```text
native execution -> retained state -> bidirectional communication
                 -> persistent interrupt-driven runtime
```

## The four chapters

| Chapter | Capability | Article | Physical evidence |
|---|---|---|---|
| 1. Voice | reset-vector fetch, native ROM execution, diagnostic I/O | [The V30 Said Hello](01-the-v30-said-hello.md) | [PC1-C0C0-H validation](../validation/pc1c0c_native_bios_hello_validation.md) |
| 2. Memory | bounded word and byte-lane write/read/consume coherence | [Now It Can Remember](02-now-it-can-remember.md) | [PC1-C0C1-B2-C validation](../validation/pc1c0c1b2c_multi_slot_ram_validation.md) |
| 3. Conversation | Codex-initiated 64-byte HID mailbox exchange | [Codex Crosses Forty Years](03-codex-crosses-forty-years.md) | [AI-B3 validation](../validation/ai_b3_codex_initiated_greeting_validation.md) |
| 4. Continuity | native `STI`/`HLT`, physical INTR/INTA, persistent heartbeat | [The V30 Is Still Alive](04-the-v30-is-still-alive.md) | [1 MHz companion-runtime validation](../validation/companion_runtime_1mhz_validation.md) |

## Reading and evidence policy

These articles preserve the narrative form of the LinkedIn series. Measured
claims link to their corresponding validation documents, which remain the
engineering authority for exact firmware identities, retained artifacts,
scope, and limitations.

The articles must not be used to promote a planned capability to an accepted
one. If a later run changes an interpretation, update the validation record
first and add a dated clarification here instead of silently rewriting
historical evidence.
