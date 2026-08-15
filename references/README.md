# References

This directory contains reference indexes and notes, not copies of third-party manuals unless redistribution rights are clear.

## Primary specification sources

### NEC V20/V30

- NEC V20/V30 original user manuals and datasheets — normative source for CPU electrical behavior, reset requirements, bus cycles, READY handling, INTA behavior, and timing.

### Waveshare RP2350-PiZero

- Official Wiki: `https://www.waveshare.net/wiki/RP2350-PiZero`
- Official schematic linked from the Wiki.
- Pico SDK 2.3.0 board definition: `waveshare_rp2350_pizero`.

Use these for RP2350-PiZero GPIO allocation, native USB, MicroSD, DVI, PIO-USB, flash, PSRAM chip-select, and board-level bring-up.

### Original Pi86 / Homebrew8088 HAT

- Repository: `https://github.com/homebrew8088/pi86`
- KiCad board source: `Kicad/Single Int/Single Chip.kicad_pcb`
- KiCad schematic: `Kicad/Single Int/Single Chip.kicad_sch`
- Original Pi86 source code and GPIO mapping.

Reference priority for HAT electrical questions:

1. Current `Single Chip.kicad_pcb` for actual current upstream board nets/routing.
2. Current `Single Chip.kicad_sch` as a secondary schematic representation.
3. Original Pi86 source for software-visible signal mapping and behavior.
4. Historical `.sch`, `.net`, backup, and Gerber files only when investigating revision history.

Important: legacy `Single Chip.sch` explicitly contains `This does not match PCB`; do not treat it as the authoritative current schematic.

The project's physical HAT is marked Copyright 2021 EMM, while current upstream KiCad PCB data contains 2023 revision/silkscreen information. Current upstream PCB data therefore corroborates the electrical design and GPIO mapping but does not eliminate the need to verify the physical 2021 board during Gate 2.

## Temporary GPIO fixture

- Fixture: `Pi ALL GPIO TEST BOARD (A)` Raspberry Pi 40-pin LED GPIO test board.
- Purpose: Gate 1 GPIO0-GPIO27 mapping / continuity validation only.
- Physical fixture inspection shows per-channel resistor markings `01B` and `102`; both represent approximately 1 kOhm.
- Remove the fixture before V30 electrical/timing tests.

Product-page captures, fixture photographs, and bench evidence belong in the Google Drive evidence archive rather than this source repository.

## Related implementation reference

- ArduinoX86: `https://github.com/dbalsom/arduinoX86`
  - Physical x86/V20/V30 bus-control concepts.
  - Cycle-level observation and validation approaches.

## Secondary CPU metadata

- cpu-db: `https://github.com/zymos/cpu-db`
  - Useful for structured CPU identification, variants, package, frequency, voltage, and historical metadata.
  - Not a normative timing/electrical source.

## Project evidence archive

Large/manual/reference documents, hardware photographs, scope captures, bring-up logs, benchmarks, and DOS boot evidence are maintained in the project's Google Drive archive rather than duplicated in this source repository.
