# PC1-C0C0 Descriptor-Fed SRAM ROM Validation

- Date: 2026-08-19
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_sram_rom_execution`
- Firmware commit: `7c27493`
- UF2 size: 90,112 bytes
- UF2 SHA-256: `AF489BCD3DAFB45DC55B4FD4BF54F3DC870263F3FC1AD2150ABA66C7BE9D953E`
- ROM size: 28 bytes
- ROM SHA-256: `90FD54D3B2E6A9E0106CCC4218CDC7902E3F68366888D3B8046235A6B7CBCB35`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Prove that the physical V30 can execute a bounded program at `F0000` through
the PIO-direct bus engine without an M33 current-cycle round trip. The program
loads three immediate values, writes them to three physical addresses, and
then enters a repeatable `JMP $` checkpoint.

The response path was:

```text
SRAM expected-key table       -> DMA -> PIO1 matcher TX FIFO
SRAM response-descriptor table -> DMA -> PIO1 responder TX FIFO
PIO1 exact early-T1 match     -> IRQ  -> scattered AD/PINDIRS
PIO0 passive address/R2 data  -> DMA  -> SRAM evidence
```

## CPU-visible evidence

The physical trace proves more than sequential instruction fetch:

| Evidence | Physical result | Meaning |
|---|---:|---|
| First reset fetch | `FFFF0`, word memory read | clean post-reset epoch |
| Far-jump target | `F0000` | reset-vector instruction consumed |
| Memory write | `F0100 = 1234` | immediate load and DS:offset formation |
| Memory write | `F0102 = 5678` | second register/immediate path |
| Memory write | `F0104 = ABCD` | third register/immediate path |
| Checkpoint | four reads at `F001A`, data `FEEB` | repeatable `JMP $` execution |

All 20 expected key/response pairs were consumed. Both response DMAs and both
PIO1 TX FIFOs ended empty. Every supported read had the expected R2 data;
there were no response deadline misses or unqualified drive commands.

The passive observer ended with 61 SRAM words: 30 complete address/R2-data
pairs plus one ASTB-high early-T1 snapshot of the next speculative `F001C`
fetch. The terminal word is explicitly framed and excluded from complete-cycle
classification. PIO0 RX FIFO residue was zero.

## Complete physical output

```text
PC1-C0C0 Descriptor-Fed SRAM ROM Execution - 0.300 MHz
RESET qualification : clock-only; matcher/responder SMs disabled
Measurement epoch   : arm after RESET clocks with CLK stopped LOW
Realtime engine     : PIO1 synchronized CLK + exact matcher + AD/PINDIRS
Response policy     : SRAM key/descriptor tables -> DMA -> PIO1 FIFOs
Current-cycle M33   : NONE
Observer path       : passive PIO0 address/R2-data -> DMA -> SRAM
ROM image           : 28 bytes at F0000; SHA-256 90fd54d3b2e6a9e0106ccc4218cdc7902e3f68366888d3b8046235a6b7cbcb35
Input synchronizers : SDK defaults

RESET clock count         = PASS
PRE-RESET EVENT LEAK      = NO
PIO1 pre-release OE       = 00200000 CLK-ONLY PASS
FIRST post-reset address  = FFFF0 PASS
FIRST cycle type          = MEMORY READ PASS
Matcher FIFO primed       = 4/4 PASS
Responder FIFO primed     = 4/4 PASS
PRE-RELEASE DMA remain    = key 16 response 16
DMA observer first address= FFFF0 PASS
FIRST response R2/F2/R3   = 00EA PASS
Far-jump target observed  = F0000 PASS
Write F0100=1234          = PASS
Write F0102=5678          = PASS
Write F0104=ABCD          = PASS
Checkpoint F001A reads   = 4/4 PASS
PIO-qualified pairs       = 20/20 PASS
POST-RUN DMA remain       = key 0 response 0 PASS
Matcher FIFO remain       = 0 PASS
Responder FIFO remain     = 0 PASS
Observer complete cycles  = 30
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
04  F0000  MEMR  WORD 01840322 006ED55B BAFA YES
05  F0002  MEMR  WORD 018C0322 0024411E F000 YES
06  F0004  MEMR  WORD 01842322 006E7156 DA8E YES
07  F0006  MEMR  WORD 018C2322 0824D14B 34B8 YES
08  F0008  MEMR  WORD 01840362 006EC91A BB12 YES
09  F000A  MEMR  WORD 018C0362 0864C547 5678 YES
10  F000C  MEMR  WORD 01842362 0C269957 CDB9 YES
11  F000E  MEMR  WORD 018C2362 046C195B A3AB YES
12  F0010  MEMR  WORD 01848322 00240902 0100 YES
13  F0012  MEMR  WORD 018C8322 0C665142 1E89 YES
14  F0100  MEMW  WORD 01840BA2 0064E1A3 1234 NO
15  F0014  MEMR  WORD 0184A322 002C0902 0102 YES
16  F0016  MEMR  WORD 018CA322 0C661142 0E89 YES
17  F0018  MEMR  WORD 01848362 00242902 0104 YES
18  F0102  MEMW  WORD 018C0BA2 0864C5E7 5678 NO
19  F001A  MEMR  WORD 018C8362 0C6E555F FEEB YES
20  F001C  MEMR  WORD 0184A362 0024A142 001C NO
21  F0104  MEMW  WORD 01842BA2 04663DFA ABCD NO
22  F001E  MEMR  WORD 018CA362 002CA142 001E NO
23  F001A  MEMR  WORD 018C8362 0C6E555F FEEB YES
24  F001C  MEMR  WORD 0184A362 0024A142 001C NO
25  F001E  MEMR  WORD 018CA362 002CA142 001E NO
26  F001A  MEMR  WORD 018C8362 0C6E555F FEEB YES
27  F001C  MEMR  WORD 0184A362 0024A142 001C NO
28  F001E  MEMR  WORD 018CA362 002CA142 001E NO
29  F001A  MEMR  WORD 018C8362 0C6E555F FEEB YES

[FIRST-CYCLE GPIO SNAPSHOTS]
phase raw_gpio  ASTB CLK IOM BUFRW INTAK UBE AD16
AF    09E6DD3F   0   1   1   0    1   0  FFF0
R1    09E6DD3F   0   1   1   0    1   0  FFF0
F1    09C6DD3F   0   0   1   0    1   0  FFF0
R2    002C1543   0   1   1   0    1   0  00EA
F2    000C1543   0   0   1   0    1   0  00EA
R3    002C1543   0   1   1   0    1   0  00EA

MEASUREMENT EPOCH   = VALID
PC1-C0C0 RESULT     = PASS
TERMINAL SAFE STATE = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Architecture conclusion

PC1-C0C0 proves bounded descriptor-fed execution at 0.300 MHz. It does not
prove arbitrary or random-access SRAM ROM service: the expected physical read
sequence is prestaged, and unrelated reads remain high-Z without advancing the
tables. The next boundary is an address-indexed service while preserving the
same exact PIO1 timing and passive PIO0/DMA evidence.
