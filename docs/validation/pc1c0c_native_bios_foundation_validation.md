# PC1-C0C0-B Native BIOS Foundation Validation

- Date: 2026-08-20
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_native_bios_foundation`
- Firmware commit: `84d7e68`
- UF2 size: 98,816 bytes
- UF2 SHA-256: `BED80E1DE174157829F360F2F55A8490FB189536DD259FA2FE6556866DDB649A`
- ROM size: 40 bytes
- ROM SHA-256: `434B33DD998208E6622CD446939DF16CBA0BC80C686F0ECFCAD1D77E831C2590`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Validate the first structured Native BIOS source tree on the physical V30.
The test uses shared platform constants and the stack-free `0xE9` diagnostic
console API rather than the earlier single-purpose HELLO assembly file.

```text
FFFF0 reset vector
  -> far jump to F0000
  -> execute firmware/v30/bios/native_bios.asm
  -> write PI86 BIOS CR LF to port 00E9h
  -> enter repeatable JMP $ checkpoint at F0026
```

The ROM is still served by the permanent descriptor-fed C0C0 engine. This
record validates the BIOS source/build/diagnostic foundation, not general
arbitrary-address C0C1 ROM service.

## Accepted evidence

| Evidence | Physical result |
|---|---:|
| Reset fetch and first response | `FFFF0`, `00EA`, PASS |
| Far-jump target | `F0000`, PASS |
| Diagnostic output | `PI86 BIOS\r\n`, 11/11 PASS |
| Final checkpoint | `F0026`, 4/4 PASS |
| Qualified ROM pairs | 26/26 PASS |
| Deadline misses | 0 |
| Unqualified drives | 0 |
| DMA/FIFO residue | 0 |
| Terminal state | RESET high, CLK low, AD high-Z |

The later speculative reads at `F0028` and `F002A` are normal V30 prefetch
around the `JMP $` checkpoint. They are outside the descriptor table, remain
unsupported/high-Z, and do not affect the accepted checkpoint.

## Complete physical output

```text
[V30 BIOS OUTPUT]
PI86 BIOS

[SUMMARY]
Measurement epoch    PASS
Reset / FFFF0 fetch  PASS
F0000 ROM execution  PASS
Diagnostic I/O 00E9  PASS
Response path        PASS
Bus ownership/safety PASS
Overall result       PASS

[ENGINEERING DETAILS]

PC1-C0C0-B Native BIOS Foundation Regression - 0.300 MHz
RESET qualification : clock-only; matcher/responder SMs disabled
Measurement epoch   : arm after RESET clocks with CLK stopped LOW
Realtime engine     : PIO1 synchronized CLK + exact matcher + AD/PINDIRS
Response policy     : SRAM key/descriptor tables -> DMA -> PIO1 FIFOs
Current-cycle M33   : NONE
Observer path       : passive PIO0 address/R2-data -> DMA -> SRAM
ROM image           : 40 bytes at F0000; SHA-256 434b33dd998208e6622cd446939df16cba0bc80c686f0ecfcad1d77e831c2590
Input synchronizers : SDK defaults

RESET clock count         = PASS
PRE-RESET EVENT LEAK      = NO
PIO1 pre-release OE       = 00200000 CLK-ONLY PASS
FIRST post-reset address  = FFFF0 PASS
FIRST cycle type          = MEMORY READ PASS
Matcher FIFO primed       = 4/4 PASS
Responder FIFO primed     = 4/4 PASS
PRE-RELEASE DMA remain    = key 22 response 22
DMA observer first address= FFFF0 PASS
FIRST response R2/F2/R3   = 00EA PASS
Far-jump target observed  = F0000 PASS
Diagnostic port 00E9 bytes= 11/11 PASS
V30 diagnostic output     = "PI86 BIOS\r\n"
Checkpoint F0026 reads   = 4/4 PASS
PIO-qualified pairs       = 26/26 PASS
POST-RUN DMA remain       = key 0 response 0 PASS
Matcher FIFO remain       = 0 PASS
Responder FIFO remain     = 0 PASS
Observer complete cycles  = 44
Observer terminal T1 tail = 1 VALID
Observer FIFO residue     = 0 PASS
Response deadline misses  = 0 PASS
Unqualified drive commands= 0 PASS

[PASSIVE ADDRESS / R2-DATA TRACE]
idx address type lanes addr_raw data_raw data hit
00  FFFF0  MEMR  WORD 09C6DF3F 002C1543 00EA YES
01  FFFF2  MEMR  WORD 09CEDF3F 00240102 0000 YES
02  FFFF4  MEMR  WORD 09C6FF3F 0024D513 90F0 YES
03  FFFF6  MEMR  WORD 09CEFF3F 086EFD1F FFF6 NO
04  F0000  MEMR  WORD 01840322 082ED55F FCFA YES
05  F0002  MEMR  WORD 018C0322 002E995F E9BA YES
06  F0004  MEMR  WORD 01842322 0024411A B000 YES
07  F0006  MEMR  WORD 018C2322 0866851E EE50 YES
08  F0008  MEMR  WORD 01840362 00269907 49B0 YES
09  F000A  MEMR  WORD 018C0362 002C755B B0EE YES
10  000E9  IOW   HIGH 040016C3 00244086 5000 NO
11  F000C  MEMR  WORD 01842362 0866815F EE38 YES
12  F000E  MEMR  WORD 018C2362 0864D10B 36B0 YES
13  000E9  IOW   HIGH 040016C3 00260886 4900 NO
14  F0010  MEMR  WORD 01848322 002C755B B0EE YES
15  F0012  MEMR  WORD 018C8322 0866011F EE20 YES
16  000E9  IOW   HIGH 040016C3 0026408A 3800 NO
17  F0014  MEMR  WORD 0184A322 00649107 42B0 YES
18  000E9  IOW   HIGH 040016C3 0864408A 3600 NO
19  F0016  MEMR  WORD 018CA322 002C755B B0EE YES
20  F0018  MEMR  WORD 01848362 0C66055E EE49 YES
21  000E9  IOW   HIGH 040016C3 0024008A 2000 NO
22  F001A  MEMR  WORD 018C8362 08669907 4FB0 YES
23  000E9  IOW   HIGH 040016C3 00640086 4200 NO
24  F001C  MEMR  WORD 0184A362 002C755B B0EE YES
25  F001E  MEMR  WORD 018CA362 0C6E851E EE53 YES
26  000E9  IOW   HIGH 040016C3 00260886 4900 NO
27  F0020  MEMR  WORD 01840323 08269903 0DB0 YES
28  000E9  IOW   HIGH 040016C3 08660886 4F00 NO
29  F0022  MEMR  WORD 018C0323 002C755B B0EE YES
30  F0024  MEMR  WORD 01842323 086E015E EE0A YES
31  000E9  IOW   HIGH 040016C3 00644886 5300 NO
32  F0026  MEMR  WORD 018C2323 0C6E555F FEEB YES
33  000E9  IOW   HIGH 040016C3 08260882 0D00 NO
34  F0028  MEMR  WORD 01840363 00240143 0028 NO
35  F002A  MEMR  WORD 018C0363 002C0143 002A NO
36  000E9  IOW   HIGH 040016C3 00660082 0A00 NO
37  F0026  MEMR  WORD 018C2323 0C6E555F FEEB YES
38  F0028  MEMR  WORD 01840363 00240143 0028 NO
39  F002A  MEMR  WORD 018C0363 002C0143 002A NO
... 4 additional cycles retained in SRAM

[FIRST-CYCLE GPIO SNAPSHOTS]
phase raw_gpio  ASTB CLK IOM BUFRW INTAK UBE AD16
AF    09E6DD3F   0   1   1   0    1   0  FFF0
R1    09E6DD3F   0   1   1   0    1   0  FFF0
F1    09C6DD3F   0   0   1   0    1   0  FFF0
R2    002C1543   0   1   1   0    1   0  00EA
F2    000C1543   0   0   1   0    1   0  00EA
R3    002C1543   0   1   1   0    1   0  00EA

MEASUREMENT EPOCH   = VALID
PC1-C0C0-H RESULT   = PASS
TERMINAL SAFE STATE = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

The final `PC1-C0C0-H` text above is a reporting-label defect in tested commit
`84d7e68`; the preceding target title and all electrical/functional gates are
the C0C0-B foundation test. A subsequent source-only fix selects the result
label by build target.

## Conclusion

The repository now has a physically validated Native BIOS source boundary,
reproducible NASM image build, companion diagnostic API, and permanent V30
execution regression. BIOS development can proceed while descriptor-fed C0C0
remains the safety net and C0C1 develops general ROM mapping.
