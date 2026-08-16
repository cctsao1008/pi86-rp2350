# PC1-C Address-Qualified ROM Execution Plan

## Objective

Convert the PC1-B fixed-response timing front-end into a real ROM service selected by the V30 address and bus-cycle type.

PC1-B proved:

- continuous clock and post-reset clean epochs;
- DMA to PIO1 TX FIFO;
- PIO1 direct scattered-AD output and `PINDIRS` control;
- V30 instruction consumption through a CPU-visible self-loop discriminator;
- fixed/pre-staged response operation at 0.300-8.000 MHz configured clock.

PC1-C must prove:

> The V30 can fetch and execute a program from an address-qualified RP2350 ROM backend rather than consuming a transaction-count-indexed response stream.

## Why this is a new boundary

PC1-B could prepare `FEEB` before the first read. General ROM service cannot know the response until the bus address and control state are captured.

```text
PC1-B
  pre-staged word -> ALE fall -> drive

PC1-C
  ALE/T1 address -> qualify cycle -> look up ROM -> encode -> drive
```

The current HAT holds V30 `READY` high. PC1-C cannot hide a lookup miss with a wait state and must measure the complete deadline.

## PC1-C0: minimal address-qualified far jump

### ROM image

Reset vector:

```asm
; physical FFFF0
JMP FAR F000:0000
```

Encoding:

```text
FFFF0: EA 00 00 00 F0
```

Target program:

```asm
; physical F0000
MOV AX,1234h
MOV BX,5678h
MOV CX,ABCDh
checkpoint:
JMP checkpoint
```

The exact final loop address must be fixed by the assembled image and listed in the test output.

### Required transaction path

```text
PIO address/control capture
        |
        v
decode A19:A0, IO/M, DT/R, BHE, A0
        |
        v
qualified memory READ
        |
        v
internal-SRAM ROM lookup
        |
        v
encode active byte lane(s) into GPIO0-27 bitmap
        |
        v
PIO1 TX FIFO -> AD drive -> H2 release
```

### Qualification rules

- Drive AD only for a classified memory-read cycle.
- Never drive during memory writes, I/O writes, or address phases.
- Apply A0/BHE lane selection to the response.
- Release AD to high-Z at the validated boundary.
- Record unsupported or out-of-range reads explicitly.
- Do not use transaction count as the ROM address.

### Acceptance criteria

- RESET qualification and post-reset epoch are valid.
- First captured address is `FFFF0`.
- The bytes returned for `FFFF0..FFFF4` match the far-jump encoding.
- A fetch at physical `F0000` is observed after reset prefetch.
- Immediate operands at the target are returned from their actual addresses.
- The V30 reaches the assembled checkpoint loop repeatedly.
- No response is driven for unqualified cycles.
- No FIFO starvation or response-deadline miss is observed.
- Terminal state is RESET high, CLK low, AD high-Z.

## PC1-C1: Mini BIOS diagnostic output

After PC1-C0, extend the ROM with the smallest CPU-to-companion output path:

```asm
MOV AL,'O'
OUT 0E9h,AL
MOV AL,'K'
OUT 0E9h,AL
JMP $
```

Port `0xE9` is initially a project diagnostic port, not a claim of IBM PC hardware compatibility. The RP2350 captures the I/O writes and mirrors them to USB CDC.

Acceptance:

```text
V30 ROM execution       PASS
I/O writes to 00E9      'O' 'K'
USB diagnostic output   OK
final checkpoint        PASS
```

This is the first Mini BIOS signature: ROM-to-CPU-to-I/O-to-host behavior in one physical execution chain.

## Frequency policy

PC1-C begins at 0.300 MHz to maximize visibility of the dynamic address-to-data path. After functional PASS, use one UF2 to sweep:

```text
0.300, 0.600, 1.200, 2.000, 3.000, 4.000, 4.770,
5.000, 6.000, 7.000, 8.000 MHz
```

Every point must create a new reset qualification and clean measurement epoch. The sweep continues after a failure so the failure mode can be compared, but the maximum supported clock is the highest repeatable contiguous PASS point.

PC1-B's 8 MHz result remains valid even if PC1-C fails earlier; the two tests measure different response classes.

## Instrumentation

At each point record:

- first post-reset address;
- bounded ALE trace;
- cycle classification counts;
- ROM hit, unsupported-read, and deadline-miss counts;
- DMA transfer completion and FIFO starvation;
- first-cycle phase snapshots;
- far-jump target observation;
- checkpoint-loop hits;
- final safe state.

When possible, separate timing into:

```text
address capture
decode / lookup
bitmap encode or cached-word selection
PIO FIFO availability
AD data-valid observation
```

## Implementation stages

### Stage A: capture-only

Capture address and control under continuous clock without driving AD. Compare decoded results against the passive PC1-B observer and known reset sequence.

### Stage B: qualified reset-vector response

Return only the reset-vector far jump from address-qualified ROM. All other cycles use an explicit unsupported-read policy.

### Stage C: target ROM window

Serve the code at `F0000`, prove immediate operands, and reach the checkpoint.

### Stage D: diagnostic port

Capture `OUT 0E9h, AL` and publish the Mini BIOS signature over USB CDC.

### Stage E: frequency sweep

Run the integrated address-qualified path from 0.300 through 8.000 MHz and identify the first dynamic-service limit.

## Deferred work

- external PSRAM;
- writable RAM backend;
- general I/O reads;
- PIC/PIT integration under the continuous-clock engine;
- keyboard, DVI, and disk services;
- full ROM monitor command set;
- BIOS compatibility declarations;
- DOS or CP/M-86 boot.

## Exit statement

PC1-C closes only when the project can state:

> A physical V30 fetched an address-qualified far jump at `FFFF0`, executed code from the RP2350 ROM window at `F0000`, and reached a deterministic CPU-visible checkpoint through the PIO-direct bus engine.
