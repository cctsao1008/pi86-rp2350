# RISC Meets CISC: A Real NEC V30 Says Hello to Raspberry Pi RP2350

> Published: 2026-08-19
>
> Original article: [LinkedIn](https://www.linkedin.com/pulse/nec-upd70116c-8-v30-says-hello-raspberry-pi-rp2350-chia-cheng-tsao-jzlbc/)
>
> Milestone: native reset-vector and internal-SRAM ROM execution at 0.300 MHz
>

For a long time, the NEC V30 on my desk was completely silent.

It was a real processor - a physical NEC uPD70116C-8 - but a CPU alone
cannot do very much.

It needs a clock. It needs RESET sequencing. It needs memory. It needs
something to respond to every bus cycle at exactly the right moment.

Without that surrounding system, it is only a forty-pin package carrying the
potential of another era.

The question was simple:

> Could a modern RP2350 become the programmable chipset around this real
> 8086-class processor - and give it a world in which it could run again?

## The first clock

The RP2350 began by generating a deliberately slow 0.300 MHz clock.

Speed was not the objective.

At this stage, every transition mattered more than frequency. Before asking
the V30 to execute useful code, I needed to know whether RESET was qualified
correctly, whether the first address appeared at the expected location, and
whether the RP2350 could respond without fighting the processor for ownership
of the multiplexed bus.

RESET was released.

The first bus address appeared:

```text
FFFF0h
```

That was the V30 reset vector.

The processor was no longer just receiving a clock.

It was looking for its first instruction.

## The first instruction

At `FFFF0h`, the RP2350 supplied a far-jump instruction.

The physical V30 fetched it through its external bus and jumped to:

```text
F0000h
```

There, it began fetching a small native ROM program.

Not an emulated V30 instruction stream.

Not a Cortex-M33 function pretending to be an x86 processor.

The real NEC V30 was placing addresses on its pins, performing memory-read
cycles, receiving machine-code bytes, and executing them internally.

The program was intentionally small.

Its only purpose was to write a short message, one byte at a time, to
diagnostic I/O port `00E9h`.

Then the output appeared:

```text
HELLO RP2350
```

For anyone watching the terminal, it was only fourteen bytes.

For the project, it meant much more.

## What had actually happened

To produce that one line, the physical V30 had successfully:

- exited RESET;
- presented `FFFF0h` as its first post-reset address;
- performed a memory-read bus cycle;
- consumed a far-jump instruction;
- reached native code at `F0000h`;
- continued fetching executable ROM data;
- executed real V30 machine code;
- performed physical I/O-write cycles to port `00E9h`;
- transmitted every byte of `HELLO RP2350\r\n`;
- completed the test without a response-deadline miss; and
- returned to a defined electrical safe state.

The important result was not the text itself.

The important result was who had executed the code that produced it.

The V30 said hello.

## This was not CPU emulation

The RP2350 did not interpret V30 instructions.

It provided the environment around the processor.

```text
Physical NEC V30
        <->
Multiplexed address/data bus
        <->
PIO + DMA
        <->
SRAM-backed response descriptors
        <->
RP2350 supervision and validation
```

The timing-critical response path was:

```text
SRAM descriptor tables
        |
        v
DMA
        |
        v
PIO1
        |
        v
V30 AD bus
```

PIO1 controlled the exact response timing and drove the scattered AD pins
through a pre-encoded GPIO bitmap.

DMA supplied the prepared response stream.

The current V30 bus cycle did not wait for Cortex-M33 software to inspect an
address, execute a lookup, and write GPIO registers.

A separate passive observation path used PIO0 and DMA to retain bus activity
for later verification:

```text
V30 bus
   |
   v
Passive PIO0 observer
   |
   v
DMA
   |
   v
SRAM evidence
```

One path served the processor.

The other watched without interfering.

This separation was essential: a successful terminal message was not enough.
The retained trace also had to prove how the message was produced.

## Why only 0.300 MHz?

A 0.300 MHz clock was not the final performance target.

It was an engineering decision.

The first goal was not to claim speed. It was to establish a clean and
reproducible baseline:

- correct RESET behavior;
- correct first address;
- correct bus-cycle classification;
- correct ROM response;
- correct data ownership;
- no response-deadline misses;
- no unqualified drive commands; and
- a safe terminal state.

Only after those foundations were trustworthy would increasing the clock mean
anything.

A faster incorrect bus is not progress.

At this stage, deterministic correctness came first.

## Physical validation evidence

The accepted run produced:

```text
V30 diagnostic output       "HELLO RP2350\r\n"

RESET clock count           PASS
First post-reset address    FFFF0h
First cycle type            MEMORY READ
Far-jump target             F0000h
Diagnostic bytes            14 / 14
PIO-qualified pairs         30 / 30
Response deadline misses    0
Unqualified drive commands  0
Terminal safe state         PASS
```

The evidence established more than program flow.

It showed that the V30 received every required response within the qualified
timing window, that the RP2350 did not drive the bus during an unsupported
cycle, and that ownership returned to a safe state when the run ended.

This was a small program - but a complete physical execution chain.

## Why this moment matters

The goal is not simply to make an old CPU print a message.

It is to explore how much of the system surrounding a real 8086-class
processor can be rebuilt as a deterministic, software-defined chipset on the
RP2350.

Clock generation. ROM and RAM. Bus control. I/O peripherals. Interrupts.
Storage. Debugging. Host communication.

The V30 remains the processor.

The RP2350 becomes the programmable world around it.

This first successful native ROM execution established the smallest
trustworthy foundation for that architecture.

Before this run, the project contained a processor, a board, firmware, timing
diagrams, and many hypotheses.

After this run, it contained a running machine.

A very small one.

A very slow one.

But a real one.

## The first word across the bridge

The V30 was designed in an era before the RP2350, USB, Python, or modern
AI-assisted engineering tools existed.

It could not know what was waiting for it on the other side.

All it knew was the language it had always understood:

RESET. Clock. Address. Data. Machine code.

The RP2350 spoke that language carefully enough for the old processor to wake
again.

And its first words were:

```text
HELLO RP2350
```

The V30 said hello.

Now the machine around it can grow.

## Acknowledgement

Special thanks to [EMM Computers / Homebrew8088](https://www.homebrew8088.com/)
and the original Pi86 project for the V20/V30 HAT, reference design, and
inspiration that made this experiment possible.

