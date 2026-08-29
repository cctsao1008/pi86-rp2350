# ADR-0001: Use the Raspberry Pi Physical Header Position as the Hardware ABI

## Status

Accepted — 2026-08-16

## Context

The original Pi86/Homebrew8088 V20/V30 HAT was designed for a Raspberry Pi 40-pin header. The original Pi86 software uses WiringPi numbering. Raspberry Pi documentation commonly uses BCM GPIO numbering. The Waveshare RP2350-PiZero exposes a Raspberry Pi-compatible physical header but routes those physical positions to RP2350 GPIO numbers according to its own board design.

During RP2350 bring-up, BCM GPIO numbers were incorrectly treated as if they were RP2350 GPIO numbers. This produced a coherent-looking but incorrect firmware map. The error affected multiple signals, including ALE, AD7, AD4, AD6, AD8, AD12, AD15 and A16.

Because diagnostics referenced the firmware map, later tests were capable of accurately observing or driving the wrong physical signal. This created false evidence for bus contention, AD7 failure and ALE/ASTB anomalies.

The same HAT and NEC V30 were already known to work on a Raspberry Pi with a working Pi86 baseline. The failure therefore originated in the cross-platform translation layer, not in the established HAT wiring contract.

## Decision

The Raspberry Pi **physical 40-pin header position** is the canonical hardware ABI for this project.

All signal translations must use:

```text
V30 signal
  -> Raspberry Pi physical header pin
  -> target-board physical routing
  -> target MCU GPIO
```

WiringPi and BCM numbers are allowed only as reference metadata when interpreting original Pi86 sources. They are not portable hardware identifiers.

Firmware must centralize processor-bus pin definitions in `firmware/processor/processor_bus_pins.h`. Documentation must identify both the physical header position and the target RP2350 GPIO. Undocumented raw GPIO literals in processor-bus code are prohibited.

## Consequences

### Positive

- Host-board ports become mechanically traceable.
- Pi86 source numbering can no longer silently leak into RP2350 GPIO definitions.
- Hardware reviews can be performed against a physical connector rather than software-specific namespaces.
- Future host boards can preserve the HAT ABI while changing MCU GPIO routing.
- Signal-level debugging begins with identity verification rather than assumptions.

### Negative

- Mapping tables carry an extra translation step.
- PIO code using absolute GPIO waits must be audited separately because PIO assembly cannot automatically inherit every C mapping abstraction.
- A target-board change requires explicit mapping review even if the new board advertises a Raspberry Pi-compatible header.

## Required review checklist

Before accepting any pin-map change:

1. Identify the V30/HAT signal.
2. Identify the Raspberry Pi physical header pin from the HAT design.
3. Read the target-board official pinout or schematic for that physical pin.
4. Record the target MCU GPIO.
5. Update the consolidated `docs/hardware.md` contract.
6. Update `firmware/processor/processor_bus_pins.h` and any absolute PIO pin references.
7. Search PIO and test sources for additional absolute GPIO references.
8. Re-run the minimum affected bring-up gate.

## Alternatives considered

### Use BCM GPIO as the canonical mapping

Rejected. BCM numbering belongs to Raspberry Pi SoCs and is not preserved by an RP2350 board merely because the connector is physically compatible.

### Use WiringPi numbering as the canonical mapping

Rejected. WiringPi is a software abstraction from the original reference platform and adds another translation layer.

### Use RP2350 GPIO as the canonical mapping

Rejected as the cross-platform ABI. RP2350 GPIO is valid inside the current firmware but does not describe the HAT connector independently of the host board.

## Evidence

After correcting the physical-header-to-RP2350 mapping:

- Gate 3 consistently captured the reset vector at physical `0xFFFF0`.
- Gate 4 returned the requested data words with correct pad readback and demonstrated prefetch-aware execution behavior.
- Gate 5 executed a far jump from the reset vector into an SRAM-backed ROM loop at physical `0xF0000`.
- Gate 6 executed an aligned RAM write/readback/compare branch sequence and reached the success loop.

These results supersede diagnostics made under the incorrect host GPIO mapping.
