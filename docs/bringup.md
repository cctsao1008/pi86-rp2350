# Bring-Up Plan

The bring-up sequence is intentionally gated. Do not combine multiple unverified subsystems in the same first test.

Before using any GPIO mapping in bring-up work, read [`hardware_contract.md`](hardware_contract.md). The Raspberry Pi physical 40-pin header position is the canonical cross-platform hardware ABI.

## Gate 0 — RP2350-PiZero SDK and USB debug

Configuration:

```text
PC <-USB-> RP2350-PiZero
```

Acceptance criteria:

- Pico SDK project configures successfully.
- `pi86_rp2350` builds.
- UF2 loads and runs.
- Native USB stdio enumerates.
- Baseline banner is visible on the host.

**Status: PASS**

No V30 HAT is required for Gate 0.

## Gate 1 — GPIO0-GPIO27 validation

Configuration:

```text
RP2350-PiZero
      |
Temporary GPIO LED test board
```

Use the `gpio_test` target.

Acceptance criteria:

- Each GPIO0-GPIO27 can be driven individually.
- Observed sequence matches the printed GPIO number.
- No stuck or unexpected coupled outputs are observed.

**Status: PASS**

**The V30 HAT must be removed for this test.**

## Gate 2 — V30 HAT electrical preflight

Configuration:

```text
RP2350-PiZero
      |
Original Pi86 V30 HAT
      |
NEC D70116C-8
```

Before active bus operation:

- Confirm orientation and physical seating.
- Confirm common ground.
- Keep clock inactive until firmware explicitly owns it.
- Establish safe reset state.
- Keep bidirectional AD pins from contending during initialization.

**Status: PASS**

The same HAT/CPU assembly is also known to operate with an older Raspberry Pi Pi86 baseline. This is a strong prior when debugging the new host port.

## Gate 3 — Clock, RESET, and first fetch

Initial clock must be deliberately low. Do not optimize frequency yet.

Sequence:

1. Generate controlled V30 clock.
2. Apply the NEC-required reset sequence.
3. Release RESET.
4. Observe the first address transaction.
5. Verify physical address `0xFFFF0`.

Critical milestone:

```text
RESET -> first bus fetch -> 0xFFFF0
```

### Status — PASS

After correcting the RP2350-PiZero host mapping to use Raspberry Pi physical-pin position as the canonical interface reference, `gate3_ffff0` captured the first post-reset ALE/T1 window with:

```text
ALE = 1
AD15..AD0 = 0xFFF0
A19..A16  = 0xF
decoded physical address = 0xFFFF0
```

Three consecutive ALE-high samples decoded to `0xFFFF0`, establishing a stable reset-vector address window.

## Gate 4 — Aligned 16-bit memory reads

Initial scope remains deliberately narrow:

- pi86-style software-stepped V30 clocking
- aligned 16-bit memory reads only (`A0=0`, `BHE=0`)
- internal-SRAM-backed deterministic ROM data
- no writes, I/O, odd-byte access, or PSRAM dependency

Reference ROM behavior:

```text
0xFFFF0 -> 0xFEEB   ; bytes EB FE = JMP SHORT -2
other aligned prefetch reads -> 0x9090
```

### Status — PASS

`gate4_pi86_reference` verified the first reset-vector memory-read cycle:

```text
address = 0xFFFF0
IO/M / DT/R / INTA = 1 / 0 / 1
requested data = 0xFEEB
AD readback after data CLK #1 = 0xFEEB
AD readback after data CLK #2 = 0xFEEB
```

The prefetch-aware `gate4_pi86_loop_smoke` then serviced:

```text
0xFFFF0 -> 0xFEEB
0xFFFF2 -> 0x9090
0xFFFF4 -> 0x9090
0xFFFF0 -> 0xFEEB
0xFFFF2 -> 0x9090
0xFFFF4 -> 0x9090
0xFFFF0 -> 0xFEEB
```

All seven aligned reads had correct data pad readback; the reset vector was observed three times as required. This demonstrates functional execution of the reset-vector short-jump loop with normal V30 prefetch behavior.

Earlier AD7/ALE/contention diagnostics performed under the incorrect host GPIO mapping are superseded where their interpretation depended on the wrong signal identity. Preserve those logs as debugging archaeology; do not treat them as current physical-signal evidence.

## Gate 5 — Minimal executable ROM

Goal: replace address-specific test data with a byte-addressed ROM backend in RP2350 internal SRAM and demonstrate actual V30 machine-code execution outside the reset-vector region.

ROM program:

```text
FFFF0: EA 00 00 00 F0    JMP FAR F000:0000
F0000: 90 90 EB FC       NOP, NOP, JMP SHORT -4
```

Acceptance criteria:

- first aligned memory read is physical `0xFFFF0`
- data pad readback matches the ROM word on every serviced read
- the far jump is executed
- physical `0xF0000` is observed repeatedly

### Status — PASS

`gate5_minrom` serviced 13 aligned memory reads. Physical `0xF0000` was observed three times as required, and every serviced read had correct data-pad readback.

This proves that the V30 executes code supplied by an RP2350 SRAM-backed ROM rather than merely receiving a single hard-coded reset-vector value.

## Gate 6 — Aligned RAM write/readback

Goal: add an RP2350 internal-SRAM RAM region and demonstrate that the V30 can write a word, read the same word back, compare it, and select the correct control-flow path.

Test semantics:

```text
MOV AX,1234
MOV [0200],AX
MOV AX,[0200]
CMP AX,1234
JNE FAIL
SUCCESS
```

Acceptance criteria:

- aligned memory write transaction to physical `0x00200`
- backend stores `0x1234`
- later aligned memory read returns `0x1234`
- CPU reaches SUCCESS loop at physical `0xF0020`
- FAIL loop at physical `0xF0030` is not observed

### Status — PASS

`gate6_ram_write_readback` produced:

```text
WRITE 0x00200 = 0x1234
RAM backend [0x00200] = 0x1234
READ  0x00200 = 0x1234
SUCCESS F0020 observed 3 times
FAIL F0030 not observed
```

This is CPU-side semantic verification: the V30 consumed the readback value, executed `CMP`, and selected the success branch.

## Current capability boundary

Validated:

- deterministic reset and first fetch
- pi86-style software-stepped clocking
- aligned 16-bit memory read
- aligned 16-bit memory write
- RP2350 internal-SRAM ROM backend
- RP2350 internal-SRAM RAM backend
- actual V30 control-flow execution including far jump, short jump, compare, and conditional branch

Not yet validated:

- low-byte/high-byte lane transactions
- odd-address word accesses
- general I/O transactions
- INTR/INTA behavior
- PSRAM backend timing and integrity
- Pi86 BIOS/system services
- MicroSD disk images
- DOS boot
- virtual CGA -> DVI
- performance toward 4.77 MHz and beyond

## Next development boundary

Before extending transaction coverage, consolidate the proven Gate 4–6 logic into reusable modules:

```text
V30 bus engine
  -> transaction decoder
  -> memory backend interface
     -> ROM
     -> RAM
     -> later PSRAM
```

The next functional gate should then add A0/BHE byte-lane behavior and odd-address access on top of that reusable architecture rather than copying another monolithic diagnostic.

## Retrospective and evidence

For the mapping failure, superseded diagnostics, test-design lessons, and permanent process rules, see:

- [`hardware_contract.md`](hardware_contract.md)
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md)
- [`retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md`](retrospectives/2026-08-rp2350-pi86-bringup-retrospective.md)
- GitHub Issue #14

Raw/human-readable Gate evidence is archived in the project Google Drive under `02_Bringup_Logs` and `03_Scope_Captures`, indexed by the `Pi86-RP2350 Bring-up Evidence Matrix` spreadsheet.
