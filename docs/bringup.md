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

## Current capability boundary

Validated:

- deterministic reset and first fetch
- pi86-style software-stepped clocking
- aligned 16-bit memory read/write
- low/high byte-lane memory transactions
- odd-address 16-bit memory accesses split by the V30 across two byte cycles
- reusable V30 bus transaction layer
- byte-addressed RP2350 SRAM ROM/RAM backend
- V30 control-flow execution including far jump, short jump, compare and conditional branch

Not yet validated:

- general I/O-space `IN`/`OUT` transactions
- INTR/INTA behavior
- PIC/PIT behavior
- PSRAM backend timing and integrity
- BIOS/system services
- MicroSD disk images
- DOS boot
- virtual CGA -> DVI
- performance toward 4.77 MHz and beyond

## Next development boundary — Gate 8

Gate 8 adds only one new architectural capability: V30 I/O-space transactions.

Planned semantic test:

```text
CPU OUT -> RP2350 I/O backend
CPU IN  <- same backend
CPU CMP returned value
SUCCESS / FAIL branch
```

The first I/O gate should use a deterministic synthetic port backend rather than introducing a real PC peripheral at the same time. PIC/PIT/keyboard/device semantics remain later gates.

See [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) for the dependency-driven route from I/O-space support toward BIOS and DOS boot.

## Retrospective and evidence

For the mapping failure, superseded diagnostics, test-design lessons, and permanent process rules, see:

- [`hardware_contract.md`](hardware_contract.md)
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md)
- [`retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md)
- GitHub Issue #14

Raw/human-readable Gate evidence is archived in the project Google Drive under `02_Bringup_Logs` and `03_Scope_Captures`, indexed by the `Pi86-RP2350 Bring-up Evidence Matrix` spreadsheet.
