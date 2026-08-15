# Hardware Baseline

## Host board

Waveshare RP2350-PiZero.

The project uses the Raspberry Pi-compatible 40-pin header. GPIO0-GPIO27 are the interface to the original Pi86 HAT.

Primary board references:

- Waveshare RP2350-PiZero Wiki: `https://www.waveshare.net/wiki/RP2350-PiZero`
- Waveshare RP2350-PiZero schematic linked from the official Wiki.
- Pico SDK 2.3.0 official board definition: `waveshare_rp2350_pizero`.

The RP2350-PiZero resource split relevant to this project is:

- GPIO0-GPIO27: Raspberry Pi-compatible 40-pin HAT interface.
- GPIO28-GPIO29: PIO-USB.
- GPIO30-GPIO31 and GPIO40-GPIO43: onboard MicroSD.
- GPIO32-GPIO39 and GPIO44-GPIO46: onboard DVI-related signals.
- GPIO47: PSRAM chip select.
- Native USB uses the RP2350 dedicated USB D+/D- interface rather than GPIO0-GPIO27.

The physical board silkscreen and Waveshare documentation both corroborate the GPIO0-GPIO27 40-pin-header allocation.

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

### Confirmed current-PCB electrical routing

The current `Single Chip.kicad_pcb` corroborates the project GPIO mapping and directly routes Raspberry Pi header signals to the CPU/HAT nets; no level-shifter stage is present in the documented interface path.

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

The current PCB is therefore a strong electrical/mapping corroboration, but it is not assumed to be an exact revision-identical representation of the physical 2021 board. Gate 2 continuity and voltage measurements remain required.

## Temporary GPIO test board

The Gate 1 fixture is a `Pi ALL GPIO TEST BOARD (A)` Raspberry Pi 40-pin GPIO LED test board.

Confirmed from the physical fixture and product documentation:

- Covers GPIO0-GPIO27, including physical pins 27/28 (`GPIO0`/`GPIO1`).
- Uses per-channel LED/resistor networks.
- Observed resistor markings include `01B` and `102`; both decode to approximately 1 kOhm.
- LED polarity/active state should be verified empirically during Gate 1 rather than assumed from photographs.

The fixture is for GPIO validation only. It must be removed before timing-sensitive V30 bus bring-up because its LED/resistor network adds load to the GPIO signals.

## Planned expansion

- APS6404L-class 8 MB PSRAM on the RP2350-PiZero PSRAM footprint.
- Onboard MicroSD for disk images.
- Onboard DVI for virtual CGA output.
- Native USB CDC for debug/console.
