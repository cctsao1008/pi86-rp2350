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

If `0xFFFF0` is not observed reliably, stop here and debug electrical/timing/address capture before implementing Pi86 memory or I/O services.

## Later gates

After Gate 3 passes:

- Memory read
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
