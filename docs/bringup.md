# Bring-Up Plan

The bring-up sequence is intentionally gated. Do not combine multiple unverified subsystems in the same first test.

Before using any GPIO mapping in bring-up work, read [`hardware_contract.md`](hardware_contract.md). The Raspberry Pi physical 40-pin header position is the canonical cross-platform hardware ABI.

## Gate 0 — RP2350-PiZero SDK and USB debug

**Status: PASS**

## Gate 1 — GPIO0-GPIO27 validation

**Status: PASS**

## Gate 2 — V30 HAT electrical preflight

**Status: PASS**

The same HAT/CPU assembly is also known to operate with an older Raspberry Pi Pi86 baseline. This is a strong prior when debugging the new host port.

## Gate 3 — Clock, RESET, and first fetch

Critical milestone:

```text
RESET -> first bus fetch -> 0xFFFF0
```

**Status: PASS**

After correcting the RP2350-PiZero host mapping to use Raspberry Pi physical-pin position as the canonical interface reference, `gate3_ffff0` captured a stable `0xFFFF0` ALE/T1 address window.

## Gate 4 — Aligned 16-bit memory reads

**Status: PASS**

The prefetch-aware test demonstrated repeated execution of a reset-vector short-jump loop while servicing valid aligned memory reads from the RP2350.

## Gate 5 — Minimal executable ROM

**Status: PASS**

The V30 executed code supplied by an RP2350 SRAM-backed ROM and repeatedly reached physical `0xF0000` after a far jump from the reset vector.

## Gate 6 — Aligned RAM write/readback

**Status: PASS**

CPU-semantic result:

```text
WRITE 0x00200 = 0x1234
READ  0x00200 = 0x1234
CMP succeeds
SUCCESS F0020 observed 3 times
FAIL F0030 not observed
```

## Gate 6R — Reusable bus/memory architecture regression

Goal: refactor proven Gate 6 behavior into reusable `v30_bus` and byte-addressed memory modules without changing functional behavior.

Acceptance criteria:

- reproduce Gate 6 reset fetch, aligned RAM write/readback, CPU compare and SUCCESS branch
- no FAIL branch
- no new capability required for PASS

**Status: PASS**

The refactored implementation reproduced the original Gate 6 26-cycle hardware behavior, including `0x00200 <- 0x1234`, readback, compare, and repeated `F0020` SUCCESS execution.

## Gate 7 — Byte lanes and odd-address word access

Goal: validate the V30's low/high byte-lane behavior and odd-address 16-bit memory access on top of the reusable bus/memory architecture.

Test semantics:

```text
byte write/read @ 0x0200 -> LOW lane
byte write/read @ 0x0201 -> HIGH lane
word write/read 0xBEEF @ 0x0201
  -> HIGH lane @ 0x0201 carries EF
  -> LOW  lane @ 0x0202 carries BE
CPU CMP AX,0xBEEF
SUCCESS / FAIL branch
```

Acceptance criteria:

- `A0/BHE#=0/1` for the even-address low-byte transaction
- `A0/BHE#=1/0` for the odd-address high-byte transaction
- odd word is split into two byte-lane cycles at `0x0201` and `0x0202`
- backend final bytes are `5A EF BE` at `0x0200..0x0202`
- CPU reaches SUCCESS at `0xF0040` at least three times
- FAIL at `0xF0050` is not observed

**Status: PASS**

Hardware result:

```text
Even byte 0200 LOW write/read  = YES / YES
Odd byte 0201 HIGH write/read  = YES / YES
Odd word write split 0201/0202 = YES / YES
Odd word read split 0201/0202  = YES / YES
RAM final [0200..0202]         = 5A EF BE
Success-loop hits F0040        = 3/3
Fail-loop F0050 observed       = NO
GATE 7 BYTE-LANE RESULT        = PASS
```

Inactive byte lanes are intentionally high-Z during byte reads. Full 16-bit GPIO snapshots of such reads may therefore contain unrelated values on the inactive half; only the selected lane is part of the functional acceptance criterion.

## Gate 8 — I/O-space byte transactions

Goal: validate V30 `OUT`/`IN` cycles against a deterministic synthetic I/O backend before introducing real PC peripheral semantics.

Test semantics:

```text
OUT 80h,5Ah -> LOW lane
IN  80h     -> 5Ah
CMP AL,5Ah

OUT 81h,A5h -> HIGH lane
IN  81h     -> A5h
CMP AL,A5h

SUCCESS / FAIL branch
```

Acceptance criteria:

- `IO/M=0` I/O-space cycles are classified separately from memory
- even port `80h` uses LOW lane
- odd port `81h` uses HIGH lane
- backend stores and returns `5A` / `A5`
- CPU consumes the returned values and reaches SUCCESS at `0xF0040`
- FAIL at `0xF0050` is not observed

**Status: PASS**

Hardware result:

```text
Even port 80h LOW OUT/IN        = YES / YES
Odd port 81h HIGH OUT/IN        = YES / YES
I/O backend final [80h,81h]     = 5A A5
Success-loop hits F0040         = 3/3
Fail-loop F0050 observed        = NO
GATE 8 I/O RESULT               = PASS
```

As with Gate 7, the inactive byte lane is high-Z during a byte read; only the selected lane is functionally meaningful.

## Gate 9 — Maskable interrupt acknowledge and vector entry

Goal: validate the physical V30 maskable interrupt path with a synthetic interrupt source before implementing a reusable interrupt-controller backend.

Test setup:

```text
INT vector = 20h
IVT[20h] at physical 00080h:
  offset  = 0100h
  segment = F000h
ISR       = F000:0100
marker    = byte [0300] <- 5Ah
```

Acceptance criteria:

- INT is asserted only after the CPU reaches a stable wait loop
- exactly two interrupt-acknowledge cycles are observed
- acknowledge #1 carries no vector
- acknowledge #2 supplies vector `20h`
- V30 reads IVT entries at `00080h` and `00082h`
- interrupt entry stack saves are observed at `7FFEh`, `7FFCh`, and `7FFAh`
- CPU fetches ISR code at physical `F0100h`
- ISR writes marker `5A` to physical `00300h`
- CPU reaches SUCCESS at `F0040` at least three times
- FAIL at `F0050` is not observed

**Status: PASS**

Hardware result:

```text
INT asserted / deasserted       = YES / YES
INTAK cycles observed           = 2/2
INTAK #1 / #2                   = YES / YES
Stack saves 7FFA/7FFC/7FFE      = YES / YES / YES
IVT offset/segment reads        = YES / YES
ISR fetch F0100                 = YES
ISR marker [0300]               = 5A
Success-loop hits F0040         = 3/3
Fail-loop F0050 observed        = NO
GATE 9 INTERRUPT RESULT         = PASS
```

This establishes the complete CPU-semantic interrupt-entry chain: external INT request -> two acknowledge cycles -> external vector acquisition -> IVT resolution -> state save -> ISR execution.

## Gate 9R — Reusable interrupt-controller regression

Goal: replace Gate 9's test-local interrupt-source state with a reusable `pi86_pic` module while preserving the exact proven CPU-semantic behavior.

Current `pi86_pic` scope at the time of this regression was deliberately smaller than a complete Intel 8259A model:

```text
one pending interrupt request
configured 8-bit vector
INT asserted while request is pending
INTA #1 -> no vector
INTA #2 -> vector on AD7..AD0
request clears after INTA #2 completes
```

Acceptance criteria:

- reproduce Gate 9's two-INTA sequence through `pi86_pic`
- vector `20h` appears only on acknowledge #2
- the reusable controller keeps INT asserted through acknowledge #1 and clears it only after acknowledge #2
- V30 performs the same IVT reads and stack saves as Gate 9
- CPU fetches ISR at `F0100h`
- ISR marker `[0300]` becomes `5A`
- CPU reaches `F0040` SUCCESS at least three times
- `F0050` FAIL is not observed

**Status: PASS**

Target: `gate9r_pic`

Hardware result:

```text
Serviced bus cycles             = 36/256 max
First reset-vector WORD read    = PASS
PIC IRQ raised / cleared        = YES / YES
INTAK cycles observed           = 2/2
INTAK #1 / #2                   = YES / YES
Stack saves 7FFA/7FFC/7FFE      = YES / YES / YES
IVT offset/segment reads        = YES / YES
ISR fetch F0100                 = YES
ISR marker [0300]               = 5A (expected 5A)
Success-loop hits F0040         = 3/3 required
Fail-loop F0050 observed        = NO
GATE 9R PIC REGRESSION RESULT   = PASS
```

The decisive acknowledge sequence on physical hardware was:

```text
INTA #1: no vector, PIC_INTR=1
INTA #2: vector=20h, PIC_INTR=0
```

The V30 then read IVT vector `20h` at `00080h/00082h`, saved FLAGS/CS/IP at `7FFEh/7FFCh/7FFAh`, fetched the ISR at `F0100h`, wrote marker `5A` to `00300h`, and reached the `F0040h` SUCCESS loop three times without entering `F0050h` FAIL.

## Gate 10 — 8259A-compatible initialization, masking, IRQ0 and EOI

Goal: extend the validated `pi86_pic` backend into the minimum programmable 8259A-compatible subset needed to support an IBM PC/XT-style master PIC path, without introducing PIT timing yet.

Validated scope:

```text
I/O ports 20h / 21h
ICW1 / ICW2 / ICW3 / ICW4 initialization sequence
interrupt mask register (IMR)
interrupt request register (IRR)
in-service register (ISR)
IRQ0 request input
fixed-priority resolution
INTA #1 / INTA #2 sequencing
vector = programmed ICW2 base + IRQ number
non-specific EOI
```

Explicitly deferred:

```text
rotating priority
special mask mode
poll mode
special fully nested mode
buffered mode
full cascade behavior
PIT timing / channel programming / IRQ0 generation
```

**Status: PASS**

Target: `gate10_8259a`.

Hardware result established CPU-visible ICW1-4 programming, `IMR=FEh`, IRQ0 request handling, exactly two INTA cycles with vector `20h` on INTA #2, IRR -> ISR movement, IVT/ISR execution, non-specific EOI, final `ISR=00h`, and 3/3 SUCCESS loop observations.

See [`validation/gate10_8259a_validation.md`](validation/gate10_8259a_validation.md).

## Gate 11 — Multi IRQ priority, ISR blocking, EOI recovery and IRET

Goal: validate multiple pending interrupt sources and fixed-priority in-service blocking on the physical V30 using the programmable `pi86_pic` backend.

Validated scenario:

```text
vector base = 20h
IMR = FCh
raise IRQ1 first
raise IRQ0 second
IRR = 03h

IRQ0 selected first
INTA #1 / #2 -> vector 20h
IRQ1 remains pending and blocked while ISR0 is active
ISR0 -> marker A0h -> EOI -> IRET

IRQ1 becomes serviceable
INTA #1 / #2 -> vector 21h
ISR1 -> marker A1h -> EOI -> IRET

final IRR=00h ISR=00h INTR=0
```

**Status: PASS**

Targets:

- `gate11_pic_priority` — controller-only fixed-priority preflight PASS
- `gate11_irq_priority` — physical V30 end-to-end PASS

Hardware result:

```text
Serviced bus cycles                  = 89/480 max
ICW1 / ICW2 / ICW3 / ICW4           = YES / YES / YES / YES
PIC initialized / vector base        = YES / 20h
IMR programmed                       = FCh
IRQ1 then IRQ0 raised / IRR=03h      = YES / YES
INTAK cycles                         = 4 total
IRQ0 selected first / vector 20h     = YES / YES
IRQ1 blocked while IRQ0 in service   = YES
IRQ0 ISR fetch / marker A0h          = YES / YES
IRQ0 EOI / IRQ1 becomes serviceable = YES / YES
IRQ1 selected second / vector 21h    = YES / YES
IRQ1 ISR fetch / marker A1h          = YES / YES
IRQ1 EOI                             = YES
Stack frame writes x2 7FFA/7FFC/7FFE= 2 / 2 / 2
Final IRR / ISR / INTR               = 00h / 00h / 0
Success-loop hits F002E              = 3/3
GATE 11 PHYSICAL V30 RESULT          = PASS
```

See [`bringup_gate11.md`](bringup_gate11.md) and [`validation/gate11_multi_irq_priority_validation.md`](validation/gate11_multi_irq_priority_validation.md).

## Gate 12 — Minimal programmable PIT IRQ0 validation

Goal: introduce the smallest programmable PIT-compatible channel 0 required to produce a deterministic timer event and raise IRQ0 through the already validated PIC path.

Required dependency chain:

```text
V30 programs PIT ports 43h / 40h
    -> PIT channel 0 count
    -> terminal-count event
    -> pi86_pic IRQ0
    -> INTR
    -> two INTA cycles
    -> vector 20h
    -> IVT
    -> ISR0
    -> marker
    -> EOI
    -> IRET
    -> SUCCESS
```

The PIT may not bypass `pi86_pic` by driving the V30 INTR pin directly.

Initial scope is one deterministic channel-0 one-shot path only. Periodic BIOS timing, channels 1/2, speaker, DRAM-refresh behavior, full PIT mode coverage, and BIOS time-of-day services remain deferred.

**Status: PASS — IMPLEMENTED AND VALIDATED ON PHYSICAL V30 HARDWARE**

See [`bringup_gate12.md`](bringup_gate12.md) and [`validation/gate12_pit_irq0_validation.md`](validation/gate12_pit_irq0_validation.md).

## Current capability boundary

Validated:

- deterministic reset and first fetch
- pi86-style software-stepped clocking
- aligned 16-bit memory read/write
- low/high byte-lane memory transactions
- odd-address 16-bit memory accesses split across two byte cycles
- reusable V30 bus transaction layer
- byte-addressed RP2350 SRAM ROM/RAM backend
- byte I/O-space `IN`/`OUT` on even and odd ports
- physical V30 maskable interrupt entry through INTA, IVT and ISR execution
- reusable programmable `pi86_pic` backend
- ICW1-4 / IMR / IRR / ISR / vector / EOI PIC subset
- multi-IRQ fixed-priority arbitration
- ISR priority blocking and pending lower-priority recovery
- two sequential real V30 ISR entries and `IRET` returns
- V30 control-flow execution including far jump, short jump, compare and conditional branch
- programmable PIT channel 0 and timer-driven IRQ0 through the PIC path (`Gate 12`)
- PIO-direct fixed instruction response through DMA -> PIO1 TX FIFO -> scattered AD GPIO/PINDIRS
- post-reset `EB FE` self-loop execution from 0.300 through 8.000 MHz (`PC1-B`)

Not yet validated:

- periodic BIOS timer compatibility
- advanced 8259A modes
- address-qualified continuous-clock ROM service (`PC1-C`)
- continuous-clock RAM reads/writes and byte-lane handling
- PSRAM backend timing and integrity
- BIOS/system services
- keyboard path
- MicroSD disk images / boot storage path
- DOS boot
- virtual CGA -> DVI
- sustained integrated-system performance at V30-class clock rates

## Next development boundary

PC1-C address-qualified ROM execution is the active development boundary.

PC1-B proved that the RP2350 can deliver a pre-staged fixed instruction response through PIO1 at configured clocks through 8.000 MHz. It did not prove that arbitrary V30 addresses can be captured, decoded, looked up in SRAM, and answered within the same no-wait-state bus deadline.

PC1-C must first execute a reset-vector far jump from `FFFF0` to ROM at `F0000` and expose a CPU-visible checkpoint. The following increment adds a minimal debug output port so a ROM program can emit `OK`. Periodic BIOS timer semantics remain deferred until address-qualified memory and I/O service are established on the continuous-clock front end.

See [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) for the broader dependency-driven route toward BIOS and DOS boot.

## Retrospective and evidence

For the mapping failure, superseded diagnostics, test-design lessons, and permanent process rules, see:

- [`hardware_contract.md`](hardware_contract.md)
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md)
- [`retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md)
- GitHub Issue #14

Raw/human-readable Gate evidence is archived in the project Google Drive under `02_Bringup_Logs` and `03_Scope_Captures`. Gate 11 raw evidence is stored under `02_Bringup_Logs/Gate11_MultiIRQ_Priority`.
