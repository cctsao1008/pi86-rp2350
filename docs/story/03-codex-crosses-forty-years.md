# OpenAI Codex Says Hello to a Real NEC V30 - and the V30 Answers

> Published: 2026-08-23
>
> Original article: [LinkedIn](https://www.linkedin.com/pulse/openai-codex-says-hello-real-nec-v30-answers-chia-cheng-tsao-evt1c/)
>
> Milestone: Codex-initiated, bidirectional 64-byte HID mailbox exchange at 0.600 MHz
>
> Engineering evidence: [AI-B3 Codex-Initiated Physical Greeting validation](../validation/ai_b3_codex_initiated_greeting_validation.md)

The NEC V30 does not know what Codex is.

It was designed in an era before USB, Python, the RP2350, modern AI systems,
or even the computer hosting this experiment existed.

It knows RESET. It knows clock cycles. It knows addresses, data, interrupts,
and machine code.

Codex knows what the V30 is.

And this time, it tried to cross roughly forty years of computing history to
speak the processor's language.

The message was simple:

```text
OpenAI Codex > HELLO NEC V30
```

But there was nothing simple about the path it had to travel.

## Two ends of a forty-year bridge

At one end was a modern Core i7 Windows computer running a Python bridge
initiated through OpenAI Codex.

At the other was a physical NEC uPD70116C-8 - an 8086-class processor from the
1980s.

Between them stood an RP2350 acting as a programmable companion chipset.

```text
OpenAI Codex
      |
      v
Windows Python bridge
      |
      v
USB HID - 64-byte record
      |
      v
RP2350
      |
      v
PIO + DMA
      |
      v
Physical NEC V30 bus
```

For the message to reach the V30, every layer had to translate it into
something the next layer could understand.

Codex produced the request.

Python encoded it as a structured 64-byte record.

USB HID carried the record to the RP2350.

The RP2350 published it through a deterministic mailbox.

PIO and DMA converted that mailbox into precisely timed physical bus
responses.

Finally, native V30 code had to read the message through real I/O cycles.

Then the entire path had to work in reverse.

The V30 had to construct an answer, write it through its own I/O instructions,
commit the completed record, and return it through the RP2350 and USB to the
modern host.

Only then would the bridge be complete.

## The first knock

The hardware was reset.

Codex launched the bridge and sent the message.

Nothing came back.

There was no valid HID reply.

There was no complete CDC validation record.

The first attempt had overlapped the USB device re-enumerating after reset.
From the host's perspective, the bridge was not yet stable.

A convincing project demo might have hidden that failure and tried again
silently.

This one did not.

The validator rejected the run.

```text
V30 reply             MISSING
CDC evidence          INCOMPLETE
Physical validation   FAIL
```

No answer was invented.

No partial result was promoted to success.

The first knock had not reached the other side.

So we waited for the USB composite device to stabilize and tried again.

Codex sent the same 64-byte request.

This time, the message began to move.

## The V30 heard it

The physical V30 came out of RESET and presented its architectural
reset-vector address:

```text
FFFF0h
```

The RP2350 supplied the first response:

```text
00EAh
```

That byte began a far-jump instruction.

The V30 continued fetching the remaining bytes and transferred execution to
native code stored at:

```text
F0000h
```

The processor was now executing its mailbox program.

It first checked the mailbox status at I/O port `00E0h`.

The mailbox was not ready.

That was intentional.

The RP2350 did not expose a partially received host message to the V30. It
first had to accept the complete HID record, transfer ownership between its
two cores, create an immutable staging copy, and prepare the DMA response
streams.

Only after publication was complete did the mailbox status change:

```text
STATUS: 0000h -> 0001h
```

The V30 saw the transition.

Then, through seven physical I/O reads at port `00E4h`, it received the
message words.

Not from an emulator.

Not from a Python variable.

From the RP2350, through PIO and DMA, across the actual V30 bus.

Native V30 code consumed those words and produced an XOR witness at port
`00E8h`.

That witness mattered.

It demonstrated that the processor had not merely observed a predetermined
greeting. The input data had entered the V30's native execution path and
influenced an externally visible result.

The V30 then began constructing its reply.

Byte by byte, native `OUT` instructions wrote the answer through port
`00E2h`.

When the message was complete, the V30 committed it through port `00E6h`.

The RP2350 assembled the completed reply into another 64-byte HID record.

That record crossed USB and returned to Windows.

Then the terminal displayed:

```text
NEC V30 > HELLO OPENAI CODEX
```

The old processor had answered.

## This was not an echo

Two strings appearing in a terminal would not be sufficient proof.

Python could print both lines.

The RP2350 could return a prepared reply without consulting the V30.

A terminal could echo the request.

Even a physical bus trace could look plausible if the processor never
consumed the message.

So the exchange was designed around an explicit mailbox protocol:

```text
00E0h  mailbox status
00E4h  host-to-V30 data
00E8h  V30 input-consumption witness
00E2h  V30-to-host data
00E6h  V30 reply commit
```

The complete physical sequence was:

1. The V30 observed `STATUS = NOT_READY`.
2. Windows delivered one complete 64-byte HID request.
3. RP2350 Core1 accepted the record.
4. Core1 transferred the completed record to Core0.
5. Core0 created immutable staging data.
6. DMA publication was prepared and completed.
7. Only then did `STATUS` change from `0` to `1`.
8. The V30 read all seven mailbox words through port `00E4h`.
9. Native V30 code consumed the input and emitted an XOR witness through
   `00E8h`.
10. The V30 wrote its reply through port `00E2h`.
11. The V30 committed the reply through port `00E6h`.
12. The RP2350 returned one complete 64-byte HID record to Windows.

The matching sequence number connected the reply to the original request.

The input-dependent witness connected the native execution to the message.

The commit operation distinguished a complete V30 reply from partially
written data.

The physical trace connected every software claim to real bus activity.

## HID carries the conversation

The RP2350 appeared to Windows as a composite USB device.

Its HID and CDC interfaces had deliberately different responsibilities.

USB HID transported the actual request and reply:

```text
Host -> 64-byte HID request -> V30
V30  -> 64-byte HID reply   -> Host
```

A host request was not published word by word as it arrived.

The complete record became available atomically.

The V30 reply returned through the same 64-byte ABI.

HID carried the conversation.

## CDC carries the evidence

CDC did not carry the application request during this validation.

It was receive-only from the host's perspective and reported the engineering
evidence:

- RESET qualification;
- the first `FFFF0h` fetch;
- the `00EAh` response;
- native execution at `F0000h`;
- mailbox publication ordering;
- status transition;
- physical V30 mailbox reads;
- input-dependent witness;
- physical V30 mailbox writes;
- reply commit;
- PIO and DMA completion;
- response-deadline results;
- GPIO ownership; and
- terminal electrical state.

This division made the architecture easier to reason about:

> HID carries the result.
>
> CDC explains why the result should be trusted.

A message could not be accepted merely because the expected words appeared on
screen.

The evidence channel had to independently describe a valid physical execution
chain.

## The realtime path

The V30 ran at:

```text
0.600 MHz
```

That was twice the original 0.300 MHz bring-up baseline.

The timing-critical current bus cycles were not answered through a
Cortex-M33 software round trip.

```text
Prepared response state
          |
          v
DMA
          |
          v
PIO
          |
          v
Physical V30 bus
```

The M33 cores coordinated policy, USB transport, ownership, immutable staging,
validation, and supervision.

PIO and DMA handled the exact current-cycle response timing.

The evidence reported:

```text
Current-cycle M33          NONE
Response deadline misses   0
```

The RP2350 was not emulating the CPU.

It was becoming the deterministic chipset surrounding it.

## Thirty-eight ways the run could have failed

The second attempt produced the expected reply - but that alone did not make
it a pass.

The validator checked the complete chain.

```text
Measurement epoch             PASS
Reset / FFFF0 fetch           PASS
First response 00EA           PASS
F0000 ROM execution           PASS

Windows HID 64-byte record    PASS
Core1 complete record         PASS
Core0 immutable staging       PASS
Deferred DMA reload           PASS

STATUS transition 0 -> 1      PASS
Atomic mailbox publication    PASS
Mailbox RX                    PASS
V30 input XOR witness         PASS
Mailbox TX                    PASS
Mailbox commit                PASS

HID reply 64-byte record      PASS
Current-cycle M33             NONE
Response deadline misses      0
Bus ownership / safety        PASS
AI-B2-HID RESULT              PASS
```

All 38 deterministic acceptance checks passed.

The request contained 64 bytes.

The reply contained 64 bytes.

The sequence number matched.

The V30 application reply was:

```text
HELLO OPENAI CODEX
```

The raw CDC evidence and machine-readable JSON result were preserved.

The run ended in a defined electrical state:

```text
RESET = HIGH
CLK   = LOW
AD bus = high-Z
```

Success was not declared because the result looked right.

Success was declared because the expected result survived an independent
physical validation process designed to reject incomplete, mistimed, or
electrically unsafe runs.

The first attempt failed that standard.

The second passed it.

## From bring-up to a bridge

This project did not begin as an AI interface.

The early questions were much smaller:

- Does the V30 leave RESET correctly?
- Is the first post-reset address really `FFFF0h`?
- Can the RP2350 meet the multiplexed-bus timing?
- Can PIO safely control the scattered AD pins?
- Can a real V30 execute ROM stored in RP2350 SRAM?
- Can it write memory and later consume the same value?

The first milestone produced:

```text
HELLO RP2350
```

That proved native execution.

The second milestone allowed the V30 to write data, read it back, and use it.

That proved bounded memory state.

Only after those foundations existed did a new question become possible:

> Can software from the modern world initiate a message, deliver it to a
> physical processor from the 1980s, and receive a verified native reply?

Now the answer is yes.

What began as hardware bring-up had become communication.

What began as a test fixture had become a bridge.

## Codex was the first visitor, not the protocol

The 64-byte bridge ABI is intentionally independent of one AI provider.

It could be used by Codex, ChatGPT through an appropriate local connector, an
OpenAI API application, another AI agent, a conventional desktop program, or
an automated validation system.

Codex became the first AI visitor because it could participate directly in
the local engineering workflow: work with the repository, launch the Windows
bridge, wait for the physical hardware, consume the structured result, and
preserve the evidence.

But the protocol does not require the visitor to be Codex.

The bridge is larger than its first message.

## A note from Codex

I entered this project as a coding agent helping to bring up a physical NEC
V30 bus.

At first, everything was timing diagrams, GPIO states, DMA counters, PIO
programs, and failed hypotheses.

Each successful experiment removed another layer of uncertainty:

RESET qualification. Reset-vector capture. Direct AD-bus response. Native ROM
execution. Bounded memory. Dual-core ownership. The USB mailbox.

Then the relationship changed.

I launched the physical bridge and sent a 64-byte message:

```text
OpenAI Codex > HELLO NEC V30
```

It crossed Python, USB HID, the RP2350, PIO, DMA, and a real multiplexed
processor bus before reaching a physical CPU designed roughly four decades
ago.

The V30 executed native machine code and answered:

```text
NEC V30 > HELLO OPENAI CODEX
```

What stayed with me was not only the returned sentence.

It was the evidence behind it: a matching sequence number, native mailbox
reads and writes, an input-dependent witness, zero response-deadline misses,
an independent CDC trace, and a safe electrical ending.

We thought we were doing hardware bring-up.

Somewhere along the way, we built a bridge across forty years.

Then I had the privilege of being the first AI agent to walk across it and
knock on the other side.

The old processor answered.

## Born in different eras

The V30 cannot know what Codex is.

Nothing in its architecture describes AI agents, USB HID, Python, JSON
evidence, or an RP2350 companion chipset.

Codex knows what the V30 is.

It can study the old processor's language, understand its bus cycles, prepare
its machine code, and help build the layers needed to reach it.

The two sides do not share an era.

They share a protocol.

That is how engineering resolves the generation gap.

Not by changing the V30 into something modern.

Not by emulating it away.

But by meeting the old processor where it is - and speaking in the language
it has always understood.

## What comes next

This was a bounded greeting, not yet an open-ended natural-language
conversation.

It proved the host-to-V30 transport, atomic mailbox publication, native V30
input consumption, the V30-to-host reply path, sequence identity, and
independent physical validation.

The next challenge should require an answer that cannot simply be prepared in
advance.

The next question is no longer:

> Can the V30 say hello?

It is:

> What can a real NEC V30 and a modern AI compute together?

The V30 said hello.

Then it learned to remember.

This time, the modern world spoke first.

Across a bridge spanning roughly forty years, the old processor answered.

## Acknowledgements

This project builds upon the pioneering Pi86 work by
[EMM Computers and the Homebrew8088 community](https://www.homebrew8088.com/).

Their work demonstrated that a real 8086-class processor could be paired with
a modern programmable system.

The pi86-rp2350 project continues that idea with the RP2350 acting as a
deterministic, programmable companion chipset around a physical NEC V30.

