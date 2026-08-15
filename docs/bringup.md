# Bring-Up Plan

The bring-up sequence is intentionally gated. Do not combine multiple unverified subsystems in the same first test.

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
- Measure actual V30 supply voltage.
- Keep clock inactive until firmware explicitly owns it.
- Establish safe reset state.
- Keep bidirectional AD pins from contending during initialization.

Record voltage and waveform evidence in the project bring-up log / scope-capture archive.

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

### Status — FUNCTIONAL PASS

`gate4_pi86_reference` verified the first reset-vector memory-read cycle:

```text
address = 0xFFFF0
IO/M / DT/R / INTA = 1 / 0 / 1
requested data = 0xFEEB
AD readback after data CLK #1 = 0xFEEB
AD readback after data CLK #2 = 0xFEEB
```

The prefetch-aware `gate4_pi86_loop_smoke` then serviced the sequence:

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

Gate 4 issue-level closure should still retain any explicit scope/timing evidence requirements that have not yet been archived.

## Later gates

After Gate 4 functional memory-read validation:

- Minimal executable ROM / memory backend
- Memory write
- A0/BHE byte-lane behavior
- I/O transactions
- INTR/INTA
- PSRAM backend
- Pi86 BIOS/system services
- MicroSD disk images
- DOS boot
- Virtual CGA -> DVI
- Performance optimization toward 4.77 MHz, followed by higher-clock evaluation if justified by measured timing margins
