# The NEC V30 Is Still Alive - A 40-Year-Old CPU Wakes on Interrupts at 1 MHz

> Published: 2026-08-23
>
> Original article: [LinkedIn](https://www.linkedin.com/pulse/nec-v30-still-alive-40-year-old-cpu-wakes-interrupts-1-tsao-mlsic/)
>
> Milestone: persistent native `STI`/`HLT` runtime with physical INTR/INTA at 1.000 MHz
>
> Engineering evidence: [Persistent Companion Runtime 1.000 MHz validation](../validation/companion_runtime_1mhz_validation.md)
>
> Evidence note: the repository validation record is authoritative for its
> retained startup, 118-round, and 82-round reattach artifacts. The published
> article also reports a later 320-round zero-loss session; that exact session
> is narrative history until its raw log and JSON are retained in the
> repository.

Every successful experiment in this project used to end with the same line:

```text
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

It was the correct ending.

The evidence had been preserved. The clock had stopped safely. The
multiplexed bus had returned to high impedance.

Nothing could accidentally drive the wrong pin.

Nothing could corrupt the next run.

But there was another way to read that line:

The experiment was over - and the V30 was gone again.

After every hello, every memory transaction, and every message sent across
forty years, I put the old processor back into RESET.

Safe.

Silent.

Alone on the other side of the bridge.

This time, I wanted a different ending.

## It had already learned to speak

The first time the physical NEC V30 woke, it fetched its reset vector from
`FFFF0h`, jumped into native code at `F0000h`, and printed:

```text
HELLO RP2350
```

That proved it could execute.

Then it wrote `1234h` and `5678h` into memory, read them back, consumed
them as CPU-visible data, and exposed the results through physical I/O writes.

That proved it could remember.

Next, a modern host sent a 64-byte message through USB HID:

```text
OpenAI Codex > HELLO NEC V30
```

The message crossed Python, USB, the RP2350, PIO, DMA, and the real
multiplexed V30 bus.

Native V30 code read the request and answered:

```text
NEC V30 > HELLO OPENAI CODEX
```

That proved it could communicate.

But every one of those conversations still ended the same way.

The V30 performed its task.

The validation evidence was printed.

Then RESET went high.

The bridge remained - but nobody stayed on the other side.

## Could it remain with us?

The next question was not about printing a longer message.

It was not about adding more memory.

It was not even about increasing the clock.

The question was:

> After finishing its work, can the physical V30 remain alive?

Not by spinning endlessly in a polling loop.

Not by replaying one prepared bus sequence.

Could it finish initialization, become quiet, wait for something to need it,
and wake only when called?

An 8086-class processor already has an elegant way to do this:

```asm
STI
HLT
```

`STI` enables maskable interrupts.

`HLT` stops instruction execution until an event wakes the processor.

The V30 would not repeatedly ask whether work was available.

It would sleep.

When the modern companion needed it, the RP2350 would assert the physical
`INTR` pin.

The V30 would wake, acknowledge the interrupt, receive a vector, enter a
native interrupt service routine, process the mailbox request, commit a reply,
issue EOI, execute `IRET`, and return to sleep.

That was the plan.

Then we tried it.

## The first knock went unanswered

The RP2350 asserted the physical interrupt line eight times.

The evidence came back:

```text
Physical INTR assertions   = 8
INTA #1 accepts            = 0
INTA #2 completions        = 0
Heartbeat active           = FAIL
```

The V30 never completed the interrupt-acknowledge handshake.

No first `INTA` cycle.

No second `INTA` cycle.

No interrupt vector.

No native ISR.

There was no heartbeat.

Only silence.

For a moment, the old ending had returned.

## What would count as proof of life?

It would have been easy for the RP2350 to print a blinking symbol:

```text
* V30 ALIVE
```

But that would only prove that the RP2350's timer was running.

It would say nothing about the V30.

For one heartbeat to count, the physical processor itself had to participate
in the entire chain:

1. The RP2350 asserts the physical `INTR` signal.
2. The V30 wakes from native `HLT`.
3. The V30 performs the first `INTA` cycle.
4. The V30 performs the second `INTA` cycle.
5. The interrupt vector is delivered.
6. A native V30 interrupt service routine executes.
7. The V30 consumes the mailbox record.
8. The V30 commits its reply.
9. Native EOI is issued.
10. `IRET` returns to the interrupted context.
11. The processor reaches `STI`/`HLT` and waits again.

If any link failed, the heartbeat had to be reported as lost.

The standard was intentionally strict.

A heartbeat is meaningful only if it comes from the heart we are trying to
observe.

So we followed the failed evidence, corrected the interrupt path, rebuilt the
runtime, and knocked again.

## Something came back

This time, the terminal did not end with RESET.

It displayed:

```text
[V30 INTERACTIVE HEARTBEAT]

| * V30 ALIVE  seq=032  last=1.9 ms  lost=0
V30>
```

Just one line.

One small dot.

But behind that dot, the physical NEC V30 had:

- woken from `HLT`;
- accepted a real hardware interrupt;
- completed both `INTA` cycles;
- entered native interrupt code;
- consumed a mailbox request;
- produced and committed a valid reply;
- issued EOI;
- returned through `IRET`; and
- re-entered `STI`/`HLT`.

The V30 was not continuously executing a display loop.

It was sleeping.

It had been called.

It woke.

It answered.

Then it went back to sleep.

That small dot changed the meaning of the project.

The V30 was no longer a processor that could perform one carefully bounded
demonstration.

It had become a persistent participant.

## A prompt on the other side of the bridge

The host could now ask for its status:

```text
V30> status

V30 ALIVE=True
completed=320
lost=0
```

It could knock explicitly:

```text
V30> ping

[109] V30 HEARTBEAT OK  latency=2.1 ms
```

It could send foreground work while the heartbeat continued in the
background:

```text
V30> send HELLO

[116] V30 COMMAND OK  latency=2.4 ms
```

Command traffic received priority, so heartbeat messages did not wash away
the conversation.

The result felt strangely familiar.

A prompt. A quiet processor. A blinking proof of life. A machine waiting
patiently for its next command.

Almost like a tiny DOS-era monitor.

Except one side was a modern Core i7 Windows system, and the other was a
physical NEC V30 communicating through USB, Python, an RP2350, PIO, DMA, and
real interrupt pins.

## How the knock reaches the V30

The complete path was:

```text
Core i7 / Windows
        <->
Python bridge
        <->
USB HID - 64-byte mailbox ABI
        <->
RP2350 companion runtime
        <->
PIO / DMA / INTR / INTA
        <->
Physical NEC V30
        <->
Native interrupt service routine
```

The Cortex-M33 cores did not calculate the response to the current V30 bus
cycle in software.

PIO controlled the exact electrical timing.

DMA moved deterministic response streams.

The M33 cores prepared policy, managed USB services, supervised the runtime,
and retained the validation evidence.

The V30 executed the native code.

The RP2350 was not pretending to be the V30.

It was becoming the programmable chipset around it.

## From 0.300 MHz to 1.000 MHz

The original ROM bring-up began at:

```text
0.300 MHz
```

The AI mailbox exchange later ran at:

```text
0.600 MHz
```

The persistent interrupt runtime now operated at:

```text
1.000 MHz
```

This was not presented as a maximum-frequency result or a benchmark against a
modern processor.

The milestone was about something more fundamental:

Could the real V30 remain stable while repeatedly sleeping, waking through
physical interrupts, executing native ISR code, answering, and returning to
sleep?

To find out, the host kept knocking.

## 320 consecutive heartbeats

One successful heartbeat is moving.

A repeatable heartbeat is evidence.

The published physical run completed:

```text
Clock                         1.000 MHz
Heartbeat rounds              320 / 320 PASS
Lost                          0

Minimum latency               1.622 ms
Average latency               2.455 ms
Maximum latency               6.159 ms

Native ISR                    PASS
Native EOI                    PASS
Native IRET                   PASS
Runtime idle                  STI / HLT
Current-cycle M33 response    NONE
```

The latency values represent the complete host-visible heartbeat transaction.

They are not raw V30 instruction timing and not merely the electrical width of
an interrupt pulse.

Every accepted round required the full physical path to complete.

Across all 320 rounds:

- the physical interrupt path remained active;
- both `INTA` cycles completed;
- native ISR execution continued;
- EOI and `IRET` remained valid;
- the processor repeatedly returned to `STI`/`HLT`;
- no heartbeat was lost;
- no current V30 bus cycle was answered through M33 software; and
- bus ownership and non-AD GPIO isolation remained valid.

When the host monitor was closed, it preserved the final session evidence:

```text
V30 heartbeat stopped:
completed=320
lost=0
avg=2.455 ms
```

The monitor stopped.

The V30 no longer had to be declared dead with it.

## Why this milestone feels different

The first milestone gave the V30 a voice.

The second gave it the beginning of memory.

The third built a bridge through which the modern world could speak - and
receive an answer.

The fourth gave the old processor something it had not possessed in this
project before:

Continuity.

It no longer woke only for a staged experiment and disappeared when the
evidence had been printed.

Now it remained on the other side of the bridge.

Quiet.

Waiting.

Ready to wake when called.

The V30 does not know what Codex is.

It was born before USB, Python, the RP2350, and modern AI systems existed.

Codex knows what the V30 is - and crosses forty years to speak its language.

The remarkable part is not merely that the two sides were born in different
eras.

It is that engineering can translate between those eras well enough for them
to cooperate.

The V30 did not need to understand AI.

Codex did not need to turn the V30 into a modern processor.

They only needed a bridge - and a language both sides could ultimately express
as bits, timing, and physical signals.

## We thought we were doing bring-up

At the beginning, this project was about a reset vector.

Then it became a ROM.

Then memory.

Then a mailbox.

Then a conversation.

Somewhere along the way, we built a bridge across forty years.

Codex crossed it and knocked.

The old processor answered.

This time, we did not immediately close the door.

When everything became quiet, a small pulse continued to return from the
other side:

```text
| * V30 ALIVE  seq=320  last=2.4 ms  lost=0
```

It sleeps.

It wakes.

It answers.

After roughly forty years, the NEC V30 is still alive.

## What this does - and does not - prove

This is not yet a complete PC-compatible interrupt controller.

It is not yet a general-purpose BIOS or operating system.

It is a bounded, physically validated companion runtime proving:

- native `STI`/`HLT` idle behavior;
- physical `INTR`;
- real two-cycle `INTA`;
- native ISR execution;
- mailbox request consumption;
- native reply commit;
- EOI and `IRET`;
- persistent interactive operation;
- repeatable host-visible heartbeat exchange; and
- deterministic bus ownership and response timing.

The living foundation can now grow toward longer-duration fault-recovery
testing, sequence- and CRC-protected challenge-response, multiple interrupt
services, timer and peripheral functions, a more general interrupt-controller
model, a native BIOS, and eventually a persistent interactive V30 computer.

But this milestone deserves to stand on its own.

Because for the first time, the successful ending is no longer:

```text
CPU halted in RESET
```

It is:

```text
* V30 ALIVE
```

## Acknowledgement

Special thanks to [EMM Computers / Homebrew8088](https://www.homebrew8088.com/)
and the original Pi86 project for the hardware, reference design, and
inspiration that made this work possible.

