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

Current `pi86_pic` scope is deliberately smaller than a complete Intel 8259A model:

```text
one pending interrupt request
configured 8-bit vector
INT asserted while request is pending
INTA #1 -> no vector
INTA #2 -> vector on AD7..AD0
request clears after INTA #2 completes
```

Not part of Gate 9R:

```text
ICW / OCW programming
IRR / ISR registers
interrupt mask register
priority resolver
EOI semantics
8259A port 20h/21h programming contract
PIT timing or IRQ0 generation
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

**Status: PENDING HARDWARE VALIDATION**

Target: `gate9r_pic`

## Current capability boundary

Validated:

- deterministic reset and first fetch
- pi86-style software-stepped clocking
- aligned 16-bit memory read/write
- low/high byte-lane memory transactions
- odd-address 16-bit memory accesses split by the V30 across two byte cycles
- reusable V30 bus transaction layer
- byte-addressed RP2350 SRAM ROM/RAM backend
- byte I/O-space `IN`/`OUT` on even and odd ports
- physical V30 maskable interrupt entry through two INTAK cycles, IVT lookup and ISR execution
- V30 control-flow execution including far jump, short jump, compare and conditional branch

Not yet validated:

- reusable interrupt-controller regression (`Gate 9R`)
- full 8259A-compatible programming/mask/priority/EOI semantics
- PIT behavior and timer-driven IRQ0
- PSRAM backend timing and integrity
- BIOS/system services
- keyboard path
- MicroSD disk images / boot storage path
- DOS boot
- virtual CGA -> DVI
- performance toward 4.77 MHz and beyond

## Next development boundary

Do not add PIT behavior until Gate 9R has reproduced Gate 9 on real hardware.

After Gate 9R passes, choose the next gate according to the minimum BIOS/DOS dependency needed at that point. A one-shot PIT-to-interrupt test can build on the reusable controller core, while true IBM PC/XT BIOS compatibility will eventually require the broader 8259A programming contract listed above.

See [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) for the dependency-driven route toward BIOS and DOS boot.

## Retrospective and evidence

For the mapping failure, superseded diagnostics, test-design lessons, and permanent process rules, see:

- [`hardware_contract.md`](hardware_contract.md)
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md)
- [`retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md)
- GitHub Issue #14

Raw/human-readable Gate evidence is archived in the project Google Drive under `02_Bringup_Logs` and `03_Scope_Captures`, indexed by the `Pi86-RP2350 Bring-up Evidence Matrix` spreadsheet.
