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
  pre-staged word -> ASTB fall -> drive

PC1-C
  ASTB/T1 address -> qualify cycle -> look up ROM -> encode -> drive
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
decode A19:A0, IO/M, BUFR/W, UBE, A0
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
- Apply A0/UBE lane selection to the response.
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
- bounded ASTB trace;
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

### PC1-C0A: Address Capture

**Status: PASS on physical V30 hardware at 0.300 MHz.**

Capture address and control under continuous clock without driving AD. Compare decoded results against the passive PC1-B observer and known reset sequence.

Implementation target:

```text
pc1c_address_capture
```

The 0.300 MHz test uses PIO0 for the clock, paired address/control capture, and first-cycle phase snapshots. PIO1 and DMA are unused, every AD pin remains SIO input, and input synchronizers remain at their SDK defaults.

Build:

```bash
cmake --build build --target pc1c_address_capture
```

Flash:

```text
build/tests/performance_characterization_1_extended/pc1c_address_capture.uf2
```

Required result:

```text
RESET clock count          = PASS
PRE-RESET EVENT LEAK       = NO
FIRST post-reset address   = FFFF0 PASS
FIRST cycle type           = MEMORY READ PASS
AD bus ownership           = PASSIVE PASS
MEASUREMENT EPOCH          = VALID
PC1-C ADDRESS CAPTURE RESULT = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

Physical evidence is recorded in [`validation/pc1c0a_address_capture_validation.md`](validation/pc1c0a_address_capture_validation.md).

### PC1-C0B: Qualified Reset-Vector Response

**Status: PASS on physical V30 hardware at 0.300 MHz.**

Return only the reset-vector far jump from address-qualified ROM. All other cycles use an explicit unsupported-read policy.

Implementation target:

```text
pc1c_qualified_reset_vector
```

The first 0.300 MHz implementation deliberately keeps the lookup path simple and observable:

```text
PIO1 synchronized CLK
  + early-T1 exact raw-GPIO matcher
  -> internal PIO IRQ on qualified match
  + prestaged response SM
  -> encoded scattered AD bitmap + PINDIRS at ASTB fall

PIO0 passive address observer
  -> DMA
  -> bounded SRAM trace
```

The matcher and responder each receive four FIFO words before RESET release.
The matcher holds the current expected key until the physical early-T1 snapshot
matches exactly, then wakes the responder through an internal PIO IRQ. Unrelated
cycles remain high-Z and do not advance the sequence. The fourth pair qualifies
the `F0000` far-jump target and carries a no-drive sentinel.

The reset-ROM backend precompiles exact raw-GPIO address/control keys and
GPIO0-27 response descriptors before RESET release. The entire current-cycle
qualification and response path stays inside PIO1. M33 performs no lookup or
FIFO submission while the V30 is running; PIO0 and DMA independently retain
the evidence used to decode and print the trace after RESET is reasserted.

The first physical C0B run confirmed correct post-fall address classification,
but the response command missed R2 because the paired capture was not published
until ASTB had already fallen. An immediate ASTB-rise experiment then proved
too early: AD0-AD15 were mostly present but A16-A19 had not settled. C0B
therefore samples the address and minimum-mode controls eight PIO cycles after
ASTB rises (about 53 ns at the default 150 MHz PIO clock), while CLK remains
low. This is still a lookup of the current physical cycle; it does not predict
the next address or index a transaction-count response stream.

The first AF-to-F1-gated M33 build submitted commands late and consumed only the
first two reset-vector words. A raw-key M33 revision qualified all three words,
but two submissions were still late and the first response missed the physical
read window. That establishes the current-cycle M33 round trip—not address
decoding—as the remaining boundary. The PIO-local revision removes that round
trip while retaining physical address/control qualification.

The six-byte reset ROM is:

```text
FFFF0: EA 00 00 00 F0 90
```

This executes `JMP FAR F000:0000`; the sixth byte is padding for the third
16-bit fetch. PIO1 drives only qualified memory reads overlapping these six
bytes. Every other ASTB cycle receives an explicit no-drive command and remains
high-Z. Default GPIO input synchronizers remain enabled.

Build:

```bash
cmake --build build --target pc1c_qualified_reset_vector
```

Flash:

```text
build/tests/performance_characterization_1_extended/pc1c_qualified_reset_vector.uf2
```

Required result:

```text
FIRST post-reset address  = FFFF0 PASS
PIO1 pre-release OE       = 00200000 CLK-ONLY PASS
FIRST cycle type          = MEMORY READ PASS
Matcher FIFO primed       = 4/4 PASS
Responder FIFO primed     = 4/4 PASS
DMA observer first address= FFFF0 PASS
DMA observer FIFO residue = 0 PASS
FIRST response at R2/F2/R3= 00EA PASS
Required ROM hit mask     = 07 PASS
Response deadline misses  = 0 PASS
PIO-qualified pairs       = 4/4 PASS
Responder FIFO remain     = 0 PASS
Far-jump target observed  = F0000 PASS
MEASUREMENT EPOCH         = VALID
PC1-C0B RESULT            = PASS
TERMINAL SAFE STATE       = PASS
```

Reaching `F0000` proves CPU-visible consumption of the address-qualified reset
vector. C0B does not yet serve code at `F0000`; that remains the C0C boundary.

Physical evidence is recorded in
[`validation/pc1c0b_qualified_reset_vector_validation.md`](validation/pc1c0b_qualified_reset_vector_validation.md).

### PC1-C0C: SRAM ROM Execution

**PC1-C0C0 descriptor-fed micro-ROM: PASS at 0.300 MHz.**

The V30 executed a 28-byte NASM image at `F0000`, loaded three immediate
values, wrote `1234`, `5678`, and `ABCD` to physical `F0100`, `F0102`, and
`F0104`, then produced four qualified fetches of the `JMP $` checkpoint at
`F001A`. Two DMA channels fed bounded SRAM key/descriptor tables into the
PIO1 matcher and responder; PIO0 and a third DMA channel retained independent
address and R2-data evidence. The M33 did not participate in current-cycle
service.

This result proves execution from a bounded, address-qualified descriptor
sequence. It does not claim arbitrary/random-access SRAM ROM service; that is
the next PC1-C0C increment.

Physical evidence is recorded in
[`validation/pc1c0c_descriptor_fed_sram_rom_validation.md`](validation/pc1c0c_descriptor_fed_sram_rom_validation.md).

### PC1-C1: Diagnostic Port

Capture `OUT 0E9h, AL` and publish the Mini BIOS signature over USB CDC.

### PC1-C2: Address-Qualified Frequency Sweep

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
