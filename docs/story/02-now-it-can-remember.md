# The V30 Said Hello. Now It Can Read and Write Memory

> Published: 2026-08-22
>
> Original article: [LinkedIn](https://www.linkedin.com/pulse/v30-said-hello-now-can-read-write-memory-chia-cheng-tsao-7pb4c/)
>
> Milestone: bounded same-run word and byte-lane RAM coherence at 0.300 MHz
>
> Engineering evidence: [PC1-C0C1-B2-C Multi-Slot / Byte-Lane RAM validation](../validation/pc1c0c1b2c_multi_slot_ram_validation.md)

The first time the physical NEC V30 woke up, it said:

```text
HELLO RP2350
```

That moment proved something important.

The processor had come out of RESET, fetched its first instruction from
`FFFF0h`, jumped to native code at `F0000h`, and executed real V30 machine
code while an RP2350 provided the programmable chipset around it.

The V30 could speak.

But after the message was printed, nothing remained.

There was no state to carry forward. No value written and recovered. No
evidence that the machine could remember anything it had done.

A processor that can only follow a fixed ROM stream is alive - but it is not
yet much of a computer.

So the next question became:

> Can the V30 create a piece of state, retrieve it later, and actually use what
> it wrote?

## Writing a number is not enough

The first program asked the V30 to write a 16-bit value into memory:

```text
00100h <- 1234h
```

On the physical bus, the write cycle appeared.

The address was correct. The data was correct. The control signals indicated
a memory write.

But observing that cycle was not yet proof of working memory.

It only proved that the V30 had placed `1234h` onto the bus.

A logic analyzer could see it. The RP2350 could record it. The validation code
could print it.

But could the V30 get that value back?

That distinction matters.

Memory is not proven when a processor writes a value.

Memory is proven when the processor later asks for the same address - and
receives the state it created.

## The V30 asked for it back

After the write completed, the native program performed a read from the same
location:

```text
00100h -> 1234h
```

The RP2350-backed memory path recognized the qualified access and returned the
stored word through the physical V30 data bus.

The V30 received `1234h`.

That was closer.

But even a correct-looking read trace leaves one more question:

> Did the V30 actually consume the returned data, or did the observer merely
> see the RP2350 drive the expected pattern?

To answer that, the program took the value it had read and wrote it to an
externally visible diagnostic I/O port:

```text
00E8h <- 1234h
```

Now the complete chain was visible:

```text
V30 writes 1234h to 00100h
              |
              v
Memory state is retained
              |
              v
V30 reads 00100h
              |
              v
V30 receives 1234h
              |
              v
Native code consumes the value
              |
              v
V30 writes 1234h to port 00E8h
```

That final I/O write was the witness.

The value did not merely appear on the bus twice.

The physical V30 wrote it, read it back as CPU-visible data, used it
internally, and produced it again through a separate bus operation.

For the first time, the processor had created a state and acted upon its own
memory.

## One value could still be a coincidence

A single successful value is encouraging.

It is not enough.

A hardcoded response, a misplaced descriptor, or an accidental reuse of bus
data could potentially make one transaction look correct.

So the experiment repeated the sequence at a different address with a
different word:

```text
00102h <- 5678h
00102h -> 5678h
00E8h  <- 5678h
```

Again, the physical V30:

1. wrote the value;
2. requested the same address later;
3. received the retained word;
4. consumed it in native code; and
5. exposed it through an observable I/O write.

Two independent addresses now held two independent values:

```text
00100h = 1234h
00102h = 5678h
```

The machine was no longer replaying a single successful pattern.

It was maintaining distinct pieces of state.

## But a 16-bit bus has two halves

The V30 has a 16-bit data bus.

A word access can use both byte lanes, but an 8-bit access may target only one
of them.

That introduces another test.

Could the memory path distinguish between:

```text
00104h  low-byte access
00105h  high-byte access
```

These are not electrically identical operations.

A low-byte transaction uses the lower half of the data bus.

A high-byte transaction uses the upper half and depends on the address and
byte-enable state being interpreted correctly.

If the system treated every memory operation as a simple 16-bit word transfer,
one of these cases could silently corrupt the other byte.

The experiment therefore added:

- a low-byte write and read at `00104h`; and
- a high-byte write and read at `00105h`.

Both passed.

This showed that the response engine was not only retaining 16-bit words. It
also respected the physical byte-lane behavior of the V30 bus.

## What was happening around the processor

The V30 remained the CPU.

The RP2350 provided the memory environment around it.

For each accepted transaction, the timing-critical path stayed outside a
current-cycle Cortex-M33 software round trip:

```text
Physical V30 bus
        |
        v
PIO capture and qualification
        |
        v
PIO-local indexed state and response
        |
        v
Physical V30 bus
```

The Arm cores could prepare the experiment, supervise it, retain traces,
validate results, and later provide higher-level services.

They were not asked to:

1. notice the current V30 address;
2. run C code to decide what it meant;
3. look up the response; and
4. write the GPIO pins before the bus deadline expired.

That path would be too dependent on software latency and interrupt timing.

Instead, PIO and DMA formed a deterministic data plane around the physical
processor.

```text
CPU
 |
 | policy and supervision
 v
DMA
 |
 | deterministic data movement
 v
PIO
 |
 | exact bus-cycle timing
 v
V30 bus
```

This is an unusual way to use a microcontroller.

The RP2350 was not simply running a program that reacted to another device.

Part of it was becoming a software-defined chipset.

## The physical evidence

The accepted run produced:

```text
WORD0 write 00100h = 1234h     PASS
WORD0 read  00100h = 1234h     PASS
WORD0 CPU-visible output       PASS

WORD1 write 00102h = 5678h     PASS
WORD1 read  00102h = 5678h     PASS
WORD1 CPU-visible output       PASS

Low-byte write/read            PASS
High-byte write/read           PASS

PIO-qualified pairs            52 / 52
Current-cycle M33              NONE
Bus ownership / safety         PASS
Terminal safe state            PASS
```

All 52 qualified response pairs completed successfully.

The bus remained under the correct owner.

The timing-critical response path did not depend on M33 software answering the
current cycle.

When the experiment ended, the processor and multiplexed data bus returned to
a defined safe state.

## What this does - and does not - prove

This milestone should not be overstated.

It does not yet provide a general-purpose 1 MiB RAM subsystem.

The accepted transactions belonged to a bounded sequence prepared for this
experiment. The response engine knew the expected access progression in
advance.

What the result proves is narrower, but still important:

> A physical NEC V30 can write multiple memory locations, read those values
> back within the same run, consume them as real CPU-visible data, and
> correctly perform both low- and high-byte accesses through an RP2350 PIO/DMA
> memory path.

The next architectural challenge is harder.

The system must eventually respond according to the address appearing on the
current physical bus cycle - not according to a previously learned transaction
order.

That progression looks like this:

```text
Native ROM execution
          |
          v
Bounded RAM state
          |
          v
General address-qualified memory
          |
          v
Software-defined peripherals
          |
          v
BIOS services
          |
          v
Storage and display
          |
          v
A complete V30 computer
```

The project had taken one more step along that path.

## From speaking to remembering

The first experiment gave the V30 a voice.

This one gave it the beginning of memory.

That difference is deeper than it first appears.

A fixed ROM program can only repeat what was prepared for it.

Memory allows a machine to be changed by what has already happened.

The value `1234h` did not exist in the memory system before the V30 wrote it.

The processor created that state.

Later, it returned to the same address and found the value waiting.

Then it used what it found.

For a modern computer, this is the most ordinary operation imaginable.

For a physical processor separated from its original era by roughly forty
years, running inside a newly created RP2350 bus environment, it felt like
another small door opening.

The first time, the V30 said:

```text
HELLO RP2350
```

This time, it wrote something down.

And when it looked again, the machine around it had remembered.

The V30 said hello.

Now it can remember.

And the world being built around it is starting to become a computer.

## Acknowledgement

Special thanks to [EMM Computers / Homebrew8088](https://www.homebrew8088.com/)
and the original Pi86 project for the V20/V30 HAT, reference design, and
inspiration that made this experiment possible.

