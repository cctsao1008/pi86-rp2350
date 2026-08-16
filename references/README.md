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

## BIOS and PC-compatible system references

These references are useful for defining the minimum PC-compatible environment required after the V30 memory bus is validated. They are not normative sources for NEC V30 electrical timing or Pi86 HAT signal mapping.

### skiselev/8088_bios — primary BIOS implementation reference

- Repository: `https://github.com/skiselev/8088_bios`
- Open-source BIOS for Micro 8088, NuXT, Xi 8088 and related XT-class systems.
- Includes source, CMake/build instructions, ROM images, XT-IDE integration, POST, keyboard, video, disk, serial, printer, RTC, DMA and boot-path implementations.
- Includes NEC V20-optimized BIOS images and Homebrew8088 V40 Processor Card support.

Use for:

- PC/XT-class BIOS architecture and implementation patterns;
- POST sequencing and device initialization dependencies;
- BIOS interrupt service contracts such as INT 10h, INT 13h, INT 14h, INT 16h and INT 17h;
- boot-sector/IPL flow;
- identifying the minimum RP2350-emulated peripheral set needed for BIOS/DOS bring-up.

Do not use as the normative source for:

- NEC V30 bus timing;
- A0/BHE# electrical semantics;
- Pi86 HAT GPIO mapping.

### TinyBIOS — minimal BIOS architecture reference

- Repository/site: `https://praios.lf-net.org/k4m1/TinyBIOS`
- Minimal BIOS implementation useful for studying reset entry, early initialization, POST structure and boot flow without the full complexity of a mature PC BIOS.

Use for:

- minimal BIOS skeleton;
- reset-to-POST flow;
- early boot architecture;
- identifying which BIOS services can be deferred.

### maxmalysh/simple-bios — educational/minimal BIOS reference

- Repository: `https://github.com/maxmalysh/simple-bios`
- University-course BIOS implementation with source, a 64 KiB BIOS image, Bochs configuration and Intel/AT&T syntax build scripts.

Use for:

- understanding a small BIOS codebase;
- reset-vector and initialization flow;
- comparing minimal implementation structure with more complete BIOS projects.

Treat as educational reference rather than a compatibility baseline; the project intentionally has limited build/use documentation.

### IBM PC/XT Technical Reference BIOS — historical compatibility reference

Use the IBM PC/XT Technical Reference and BIOS listings, where legally available, to understand original machine contracts, interrupt-vector conventions, BIOS Data Area behavior and PC/XT peripheral assumptions.

This is a compatibility/historical reference, not a replacement for the NEC V30 manual for CPU-specific bus timing.

### SeaBIOS — mature legacy BIOS comparison reference

Use SeaBIOS selectively when a mature legacy BIOS implementation is useful for comparison. It is intentionally not the first implementation reference for this project because its architecture and supported platform scope are substantially broader than the minimum XT-class environment required for initial DOS bring-up.

## Secondary x86 architecture references

### The Holy Book of X86

- Repository: `https://github.com/Captainarash/The_Holy_Book_of_X86`
- Covers x86 assembly, architecture, segmentation, paging, interrupts and related internals.
- License: CC BY-SA 4.0; avoid copying substantial text or exercise material into this repository unless licensing obligations are deliberately handled.

Use for:

- x86 assembly semantics;
- real-mode/segmentation background;
- interrupts and architectural concepts.

Do not use as a normative source for:

- NEC V30 electrical behavior;
- V30 bus cycles or timing;
- Pi86 HAT mapping.

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
