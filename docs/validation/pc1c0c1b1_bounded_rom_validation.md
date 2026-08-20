# PC1-C0C1-B1 Bounded Multi-Cycle ROM Validation

- Date: 2026-08-20
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_bounded_rom_window`
- Firmware commit: `0a3be2d`
- UF2 size: 88,064 bytes
- UF2 SHA-256: `2aa7f7d85568ac9026553bece57da964e2fe97661d512a12551f1640a87d725b`
- Native BIOS SHA-256: `434b33dd998208e6622cd446939df16cba0bc80c686f0ecfcad1d77e831c2590`
- Result: **PASS**

## Accepted conclusion

The physical V30 executed the 40-byte Native BIOS through a bounded
current-address selector whose lookup state restarts for every ASTB cycle.
Every execution-budget block contains the same 32-entry table plus sentinel;
the table content does not encode or predict transaction order.

The run retained 64 complete passive cycles. Thirty-two supported ROM reads
returned their exact expected words, while 32 unsupported cycles consumed a
sentinel path without PIO1 AD ownership. The CPU completed the reset-vector
far jump, printed `PI86 BIOS\r\n` through genuine `00E9h` writes, and repeated
the final checkpoint ten times. RESET qualification, DMA framing, phase
response, and terminal safety all passed.

This baseline proves multi-cycle current-address selection for the declared
live set. The physical table has 32 entries, but this image deliberately keeps
its 23 live reset/BIOS words at ordinals 1 through 23. It does not claim that
the sentinel-bearing selector meets the same-cycle deadline for a live entry
at ordinal 32. C0C1-A2 remains the separate 32-entry first-cycle result.

## Complete physical output

```text
[V30 BIOS OUTPUT]
PI86 BIOS

[SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
Current-address reads    PASS (32 hits)
Diagnostic I/O 00E9      PASS
Checkpoint loop          PASS (10 reads)
Bus ownership/safety     PASS
C0C1-B1 RESULT           PASS

[ENGINEERING DETAILS]
PC1-C0C1-B1 Bounded Multi-Cycle ROM - 0.300 MHz
Table shape              = 32 entries + sentinel
Execution budget         = 80 identical table blocks
Current-cycle M33        = NONE
RESET clock qualification= PASS
TX FIFO primed           = 4/4 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 64/64
Unsupported/high-Z cycles= 32
DMA remain pre/post      = 7756/1548
PIO1 OE pre/post         = 00000000/00000000
ROM image                = 40 bytes; SHA-256 434b33dd998208e6622cd446939df16cba0bc80c686f0ecfcad1d77e831c2590
TERMINAL SAFE STATE      = PASS

[FIRST-CYCLE PHASE TRACE]
AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA

[PASSIVE ADDRESS / R2-DATA TRACE]
00 addr=FFFF0 addr_raw=09C6DF3F data_raw=002C1543 data=00EA hit=YES expected=00EA
01 addr=FFFF2 addr_raw=09CEDF3F data_raw=00240102 data=0000 hit=YES expected=0000
02 addr=FFFF4 addr_raw=09C6FF3F data_raw=0024D513 data=90F0 hit=YES expected=90F0
03 addr=FFFF6 addr_raw=09CEFF3F data_raw=086EFD1F data=FFF6 hit=NO
04 addr=F0000 addr_raw=01840322 data_raw=082ED55F data=FCFA hit=YES expected=FCFA
05 addr=F0002 addr_raw=018C0322 data_raw=002E995F data=E9BA hit=YES expected=E9BA
06 addr=F0004 addr_raw=01842322 data_raw=0024411A data=B000 hit=YES expected=B000
07 addr=F0006 addr_raw=018C2322 data_raw=0866851E data=EE50 hit=YES expected=EE50
08 addr=F0008 addr_raw=01840362 data_raw=00269907 data=49B0 hit=YES expected=49B0
09 addr=F000A addr_raw=018C0362 data_raw=002C755B data=B0EE hit=YES expected=B0EE
10 addr=000E9 addr_raw=040016C3 data_raw=00244086 data=5000 hit=NO
11 addr=F000C addr_raw=01842362 data_raw=0866815F data=EE38 hit=YES expected=EE38
12 addr=F000E addr_raw=018C2362 data_raw=0864D10B data=36B0 hit=YES expected=36B0
13 addr=000E9 addr_raw=040016C3 data_raw=00260886 data=4900 hit=NO
14 addr=F0010 addr_raw=01848322 data_raw=002C755B data=B0EE hit=YES expected=B0EE
15 addr=F0012 addr_raw=018C8322 data_raw=0866011F data=EE20 hit=YES expected=EE20
16 addr=000E9 addr_raw=040016C3 data_raw=0026408A data=3800 hit=NO
17 addr=F0014 addr_raw=0184A322 data_raw=00649107 data=42B0 hit=YES expected=42B0
18 addr=000E9 addr_raw=040016C3 data_raw=0864408A data=3600 hit=NO
19 addr=F0016 addr_raw=018CA322 data_raw=002C755B data=B0EE hit=YES expected=B0EE
20 addr=F0018 addr_raw=01848362 data_raw=0C66055E data=EE49 hit=YES expected=EE49
21 addr=000E9 addr_raw=040016C3 data_raw=0024008A data=2000 hit=NO
22 addr=F001A addr_raw=018C8362 data_raw=08669907 data=4FB0 hit=YES expected=4FB0
23 addr=000E9 addr_raw=040016C3 data_raw=00640086 data=4200 hit=NO
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Next gate

C0C1-B2 adds bounded internal-SRAM-backed RAM, a real stack, IVT installation,
and repeated `INT 10h/AH=0Eh` plus `IRET`. The B1 selector remains a permanent
ROM/current-address regression while RAM and CDC input are developed.
