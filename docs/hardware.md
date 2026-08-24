# Hardware Baseline

## Host board

Waveshare RP2350-PiZero.

The project uses the Raspberry Pi-compatible 40-pin header. RP2350 GPIO0-GPIO27 are distributed across that header and form the interface to the original Pi86 HAT.

**Important:** Raspberry Pi compatibility here is a physical-header compatibility statement. The RP2350 GPIO number present at a given 40-pin header position is not always numerically equal to the Raspberry Pi BCM GPIO number at that same position. Pi86 signal mapping must therefore translate through the physical header pin. See `docs/pin_mapping.md`.

Primary board references:

- Waveshare RP2350-PiZero Wiki: `https://www.waveshare.net/wiki/RP2350-PiZero`
- Waveshare RP2350-PiZero schematic linked from the official Wiki.
- Pico SDK 2.3.0 official board definition: `waveshare_rp2350_pizero`.

The RP2350-PiZero resource split relevant to this project is:

- GPIO0-GPIO27: exposed across the Raspberry Pi-compatible 40-pin HAT interface.
- GPIO28-GPIO29: PIO-USB.
- GPIO28-GPIO29: onboard PIO-USB D+/D-.
- GPIO30-GPIO31 and GPIO40-GPIO43: onboard MicroSD (SDIO, or SPI using
  CLK=30, MOSI=31, MISO=40, CS=43).
- GPIO32-GPIO39 and GPIO44-GPIO46: onboard Mini HDMI / DVI (three data
  pairs, clock pair, SDA, SCL, and CEC).
- GPIO47: optional PSRAM CS1.

Canonical firmware initializes GPIO28-GPIO46 as passive inputs with RP2350
pulls disabled. MicroSD, DVI, and PIO-USB remain electrically inactive until
their service explicitly claims the pins. DVI and PIO-USB are mutually
exclusive runtime resources on this board.
- GPIO47: PSRAM chip select.
- Native USB uses the RP2350 dedicated USB D+/D- interface rather than GPIO0-GPIO27.

The physical board pinout is authoritative for translating Raspberry Pi physical header positions to RP2350 GPIO numbers.

## CPU

Installed physical CPU:

```text
NEC JAPAN
D70116C-8
V30 © NEC '84
1020VD002
```

Identification:

- Family: NEC V30
- Part: D70116C-8 / uPD70116C-8
- Package: PDIP-40
- External data bus: 16-bit
- Address bus: 20-bit
- Address/data bus: multiplexed
- Nominal maximum clock grade: 8 MHz
- Physical address space: 1 MiB

### Voltage note

The D70116C-8 is nominally a 5 V-rated device. The original Homebrew8088 HAT is marked `V20/V30 (8088/8086) 3.3V`.

The current Homebrew8088 KiCad PCB source also explicitly routes the HAT 3.3 V rail to V30 pin 40 (`VCC`). Therefore **3.3 V is confirmed as the HAT design intent**, not merely inferred from the silkscreen.

Operation of this specific D70116C-8 at 3.3 V is still treated as an empirical/project-specific configuration rather than the nominal NEC rating. Gate 2 must measure the actual VCC on the physical 2021 HAT before active bus bring-up.

The `1020VD002` line is retained as the physical unit trace marking. It is not decoded into a manufacturing date without an authoritative NEC marking-code reference.

## CPU HAT

Original Homebrew8088 Pi86 V20/V30 HAT.

Project constraints:

- Direct plug into the RP2350-PiZero 40-pin header.
- No PCB redesign.
- No pin reassignment.

### Primary HAT design reference

The Homebrew8088 repository contains the KiCad source under:

`homebrew8088/pi86/Kicad/Single Int/`

For current-source inspection, use this order:

1. `Single Chip.kicad_pcb` — primary board-routing/net reference.
2. `Single Chip.kicad_sch` — secondary schematic reference.
3. `Single Chip.sch` / historical `.net` files — historical only.

The legacy `Single Chip.sch` explicitly contains the note `This does not match PCB`; it must not override the current PCB source.

### HAT physical routing vs host GPIO numbering

The upstream Pi86 PCB sources are useful for identifying which **Raspberry Pi physical header pin** carries each HAT signal. They do not define the RP2350-PiZero GPIO number used by the replacement host.

The RP2350 firmware mapping is therefore established in two steps:

1. Pi86 HAT signal -> Raspberry Pi physical 40-pin header position.
2. That physical position -> RP2350-PiZero GPIO number using the Waveshare board pinout.

This distinction is critical because several physical positions use different BCM and RP2350 GPIO numbers.

Power and fixed control straps in the current PCB source:

| Function | Current PCB connection |
|---|---|
| V30 pin 40 VCC | 3.3 V |
| V30 pin 22 READY | 3.3 V |
| V30 pin 33 MN/MX | 3.3 V |
| V30 pin 17 NMI | GND |
| V30 pin 23 TEST | GND |
| V30 pin 31 HOLD | GND |
| Raspberry Pi physical pin 4 / 5 V | FAN `5+` rail |
| Raspberry Pi physical pin 2 / 5 V | unconnected in current PCB |

The two-pin FAN connector is therefore a separate 5 V/GND auxiliary load; the current PCB does not route the 5 V rail to V30 VCC.

### Revision caveat

The physical HAT in this project is marked Copyright 2021 EMM, while the current GitHub KiCad PCB carries 2023 silkscreen/revision data and the repository history includes the 2023 `switched back to socket` update.

The current PCB is therefore a strong physical-routing corroboration, but it is not assumed to be an exact revision-identical representation of the physical 2021 board.

## Temporary GPIO test board

The Gate 1 fixture is a `Pi ALL GPIO TEST BOARD (A)` Raspberry Pi 40-pin GPIO LED test board.

Confirmed from the physical fixture and product documentation:

- Covers the Raspberry Pi 40-pin GPIO positions used by the project.
- Uses per-channel LED/resistor networks.
- Observed resistor markings include `01B` and `102`; both decode to approximately 1 kOhm.
- LED polarity/active state should be verified empirically during Gate 1 rather than assumed from photographs.

The fixture is for GPIO validation only. It must be removed before timing-sensitive V30 bus bring-up because its LED/resistor network adds load to the GPIO signals.

## Planned expansion

- APS6404L-class 8 MB PSRAM on the RP2350-PiZero PSRAM footprint.
- Onboard MicroSD for disk images.
- Onboard DVI for virtual CGA output.
- Native USB CDC for debug/console.
