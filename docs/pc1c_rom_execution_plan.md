# PC1-C ROM Execution Plan

## Objective

Transition from PC1-B bus timing validation to real V30 ROM code execution.

PC1-B proved:

- PIO/DMA based V30 bus timing engine works
- Continuous clock operation validated up to 8 MHz configured clock
- V30 can consume instructions supplied by RP2350 bus fabric

PC1-C goal:

> Make the V30 execute a real ROM image instead of a single instruction loop.

## Architecture

```
V30
 |
 | bus
 |
RP2350
 +-- PIO bus timing
 +-- DMA data response
 +-- ROM backend
```

## PC1-C0 Minimal ROM Jump

Reset vector:

```
FFFF0: EA 00 00 00 F0
```

Equivalent:

```
JMP FAR F000:0000
```

ROM entry:

```
F0000:
    MOV AX,1234h
    MOV BX,5678h
    MOV CX,ABCDh
    JMP $
```

## PASS Criteria

Expected fetch sequence:

```
FFFF0
FFFF2
FFFF4
F0000
F0002
F0003
...
```

Expected execution:

- FAR jump succeeds
- sequential ROM fetch works
- immediate data fetch works
- branch instruction executes

## Roadmap

PC1-C0: ROM jump validation

PC1-C1: Mini BIOS execution

PC1-C2: RAM subsystem

PC1-C3: BIOS services

PC1-C4: DOS/CPM-86 compatibility exploration

## Design Direction

The project evolves from a pi86 port into a hardware-assisted V30 companion platform using RP2350 PIO/DMA as a programmable chipset.
