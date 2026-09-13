# Hardware Baseline

## Host board

Waveshare RP2350-PiZero.

The project uses the Raspberry Pi-compatible 40-pin header. RP2350 GPIO0-GPIO27 are distributed across that header and form the interface to the original Pi86 HAT.

**Important:** Raspberry Pi compatibility here is a physical-header compatibility statement. The RP2350 GPIO number present at a given 40-pin header position is not always numerically equal to the Raspberry Pi BCM GPIO number at that same position. The locked translation table and electrical ownership rules are defined below.

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
- Primary XIP NOR: Winbond W25Q128JV, 16 MiB. The canonical layout reserves
  `0x000000-0x3FFFFF` for firmware and exposes `0x400000-0xFFFFFF` as the
  12 MiB `RP-FLASH` FAT16 volume (`flash:/`).

Canonical firmware initializes GPIO28-GPIO46 as passive inputs with RP2350
pulls disabled. MicroSD, DVI, and PIO-USB remain electrically inactive until
their service explicitly claims the pins. DVI and PIO-USB are mutually
exclusive runtime resources on this board.
- GPIO47: PSRAM chip select.
- Native USB uses the RP2350 dedicated USB D+/D- interface rather than GPIO0-GPIO27.

The physical board pinout is authoritative for translating Raspberry Pi physical header positions to RP2350 GPIO numbers.

### Physical feature flags

The canonical build separates three different facts:

1. `RP86_HAS_*` says whether hardware is physically populated or provided;
2. runtime detection says whether a configured device actually responds; and
3. the capability table says whether firmware support is implemented.

The Waveshare board defaults are:

| CMake option | Default | Meaning |
|---|---:|---|
| `RP86_HAS_EXTERNAL_PSRAM` | `OFF` | optional APS6404L-class device is not yet populated |
| `RP86_HAS_SDCARD` | `ON` | onboard MicroSD socket is present |
| `RP86_HAS_DVI` | `ON` | onboard Mini HDMI/DVI connector is present |
| `RP86_HAS_PIO_USB` | `ON` | onboard GPIO28/29 PIO-USB connector is present |

A socket or connector being present does not claim its GPIOs and does not
mean its service is implemented. Until claimed, SD, DVI, and PIO-USB remain
passive inputs. After the PSRAM device is soldered, configure a build with:

```bash
cmake -S . -B build -DRP86_HAS_EXTERNAL_PSRAM=ON
```

The runtime must still report successful PSRAM detection before any Host
operation targeting the PSRAM capacity tier is accepted. PSRAM absence must not
block Internal-SRAM-backed workload upload, execution, or shared memory.

## Physical interface contract

The Raspberry Pi **physical 40-pin header position** is the cross-platform
hardware ABI. The translation chain is always:

```text
8086-class signal
  -> Raspberry Pi physical header pin
  -> RP2350-PiZero board routing
  -> RP2350 GPIO
```

These numbering domains are different:

```text
WiringPi number != Raspberry Pi BCM GPIO != physical header pin != RP2350 GPIO
```

A Raspberry Pi BCM number is reference-platform metadata. It must never be
copied directly into an RP2350 GPIO definition unless the Waveshare physical
pinout independently confirms the mapping.

### Locked signal mapping

The original HAT is not redesigned or remapped.

| Signal | RPi physical pin | RPi BCM metadata | RP2350 GPIO | Normal direction |
|---|---:|---:|---:|---|
| CLK | 40 | 21 | 21 | RP2350 -> processor |
| RESET | 36 | 16 | 16 | RP2350 -> processor |
| ASTB | 32 | 12 | 9 | processor -> RP2350 |
| IO/M | 24 | 8 | 8 | processor -> RP2350 |
| BUFR/W | 26 | 7 | 7 | processor -> RP2350 |
| UBE | 22 | 25 | 25 | processor -> RP2350 |
| INTR | 38 | 20 | 20 | RP2350 -> processor |
| INTAK | 28 | 1 | 1 | processor -> RP2350 |
| AD0 | 37 | 26 | 26 | Bidirectional |
| AD1 | 35 | 19 | 19 | Bidirectional |
| AD2 | 33 | 13 | 13 | Bidirectional |
| AD3 | 31 | 6 | 6 | Bidirectional |
| AD4 | 29 | 5 | 15 | Bidirectional |
| AD5 | 27 | 0 | 0 | Bidirectional |
| AD6 | 23 | 11 | 10 | Bidirectional |
| AD7 | 21 | 9 | 12 | Bidirectional |
| AD8 | 19 | 10 | 11 | Bidirectional |
| AD9 | 15 | 22 | 22 | Bidirectional |
| AD10 | 13 | 27 | 27 | Bidirectional |
| AD11 | 11 | 17 | 17 | Bidirectional |
| AD12 | 7 | 4 | 14 | Bidirectional |
| AD13 | 5 | 3 | 3 | Bidirectional |
| AD14 | 3 | 2 | 2 | Bidirectional |
| AD15 | 8 | 14 | 4 | Bidirectional |
| A16 | 10 | 15 | 5 | processor -> RP2350 |
| A17 | 12 | 18 | 18 | processor -> RP2350 |
| A18 | 16 | 23 | 23 | processor -> RP2350 |
| A19 | 18 | 24 | 24 | processor -> RP2350 |

Firmware definitions live in `firmware/bus/processor_bus_pins.h` and must remain
consistent with this table. AD0-AD15 are intentionally scattered in RP2350
GPIO space; realtime code uses snapshots, masks, and lookup-based packing
instead of slow per-pin operations.

### Historical mapping correction

An early RP2350 mapping treated Raspberry Pi BCM numbers as RP2350 GPIO
numbers. The affected signals were:

| Signal | Incorrect GPIO | Correct GPIO |
|---|---:|---:|
| ASTB | 12 | 9 |
| AD4 | 5 | 15 |
| AD6 | 11 | 10 |
| AD7 | 9 | 12 |
| AD8 | 10 | 11 |
| AD12 | 4 | 14 |
| AD15 | 14 | 4 |
| A16 | 15 | 5 |

Correcting this translation restored deterministic reset fetch, executable
ROM, memory reads, and RAM write/readback. Historical results that depended on
the incorrect identities are superseded unless explicitly revalidated.

### Firmware and ownership rules

1. `firmware/bus/processor_bus_pins.h` is the firmware source of processor GPIO constants.
2. Bus code uses `RP86_PROCESSOR_PIN_*` definitions rather than undocumented literals.
3. Absolute GPIO references in PIO must name the corresponding signal and
   physical header pin.
4. Any mapping change requires simultaneous review of this document,
   `firmware/bus/processor_bus_pins.h`, and affected PIO programs.
5. A new host board must rebuild the translation from physical header pins;
   BCM numbering is not a portable intermediate ABI.
6. Before diagnosing timing or protocol behavior, prove signal identity,
   direction, ownership, and electrical state in that order.

AD0-AD15 must be high-Z whenever the physical processor owns the bus. RP2350
data is prepared before output enable. PIO owns timing-critical data and
direction transitions; DMA may feed PIO FIFOs but must not write SIO registers.
PIO output windows must be proven to affect only AD pins. The accepted runtime
uses a free-running PIO-generated processor clock; M33 software does not step or
answer current bus cycles.

A bounded validation may finish at `RESET=HIGH`, `CLK=LOW`, AD high-Z. The
persistent runtime instead leaves the processor in validated `STI`/`HLT` idle,
with AD high-Z and interrupt service armed.

### Fixed HAT connections

| Processor pin/function | Connection |
|---|---|
| Pin 40 VCC | 3.3 V |
| Pin 22 READY | 3.3 V |
| Pin 33 mode strap | 3.3 V |
| Pin 17 NMI | GND |
| Pin 23 TEST/POLL | GND |
| Pin 31 HOLD | GND |

The upstream PCB routes Raspberry Pi physical pin 4 (5 V) only to the auxiliary
FAN rail. Physical pin 2 is unconnected in the referenced source.

### Source hierarchy

Resolve mapping discrepancies in this order:

1. physical working-system evidence;
2. target-board official pinout or schematic;
3. HAT PCB routing;
4. original Pi86 software mapping;
5. WiringPi/BCM translation tables;
6. historical schematics and netlists;
7. inference.

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

This distinction is critical because several physical positions use different BCM and RP2350 GPIO numbers. Fixed straps and auxiliary power routing are consolidated in the physical interface contract above.

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

The fixture is for GPIO validation only. It must be removed before timing-sensitive processor-bus bring-up because its LED/resistor network adds load to the GPIO signals.

## Planned expansion

- APS6404L-class 8 MB PSRAM on the RP2350-PiZero PSRAM footprint.
- Onboard MicroSD for disk images.
- Onboard DVI for virtual CGA output.
- Native USB CDC for debug/console.
