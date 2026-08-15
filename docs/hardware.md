# Hardware Baseline

## Host board

Waveshare RP2350-PiZero.

The project uses the Raspberry Pi-compatible 40-pin header. GPIO0-GPIO27 are the interface to the original Pi86 HAT.

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

The D70116C-8 is nominally a 5 V-rated device. The original Homebrew8088 HAT is marked `V20/V30 (8088/8086) 3.3V`. Operation of this specific D70116C-8 at 3.3 V is therefore treated as an empirical/project-specific configuration, not as the nominal NEC rating.

The `1020VD002` line is retained as the physical unit trace marking. It is not decoded into a manufacturing date without an authoritative NEC marking-code reference.

## CPU HAT

Original Homebrew8088 Pi86 V20/V30 HAT.

Project constraints:

- Direct plug into the RP2350-PiZero 40-pin header.
- No PCB redesign.
- No pin reassignment.

## Temporary GPIO test board

A Raspberry Pi GPIO LED test board is available for Gate 1 only. It must be removed before timing-sensitive V30 bus bring-up because its LED/resistor network adds load to the GPIO signals.

## Planned expansion

- APS6404L-class 8 MB PSRAM on the RP2350-PiZero PSRAM footprint.
- Onboard MicroSD for disk images.
- Onboard DVI for virtual CGA output.
- Native USB CDC for debug/console.
