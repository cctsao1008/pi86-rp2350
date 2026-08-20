# PC1-C0C1-B2-A INT 10h Stack Validation

- Date: 2026-08-20
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_int10_stack`
- Firmware commit: `0cf4c49`
- UF2 size: 89,600 bytes
- UF2 SHA-256: `1ebd5b4e56c145f64f74d89e2485796717ff9d91d2eb5fc05dbab02fca4c6d73`
- V30 image: 30 bytes
- V30 image SHA-256: `c3e6e9adaf686b5c922bed2882051527d6569041d96caae82a88b46e2559588e`
- Result: **PASS**

## Accepted conclusion

Epoch A physically observed one real V30 `INT 10h` transaction. The CPU read
the IVT vector `0018h:F000h`, pushed `IP=000Ch`, `CS=F000h`, and
`FLAGS=F046h` to physical `007FAh`, `007FCh`, and `007FEh`, entered the BIOS
handler, and wrote `I` to diagnostic port `00E9h`. Stack reads were deliberately
unsupported during this learning epoch.

With RESET asserted between epochs, the companion copied those three observed
words into a fresh immutable current-address table. Epoch B repeated the same
physical execution, returned the learned stack words through PIO1, completed
`IRET`, and reached the `F000:000C` checkpoint fourteen times. Both epochs
retained 64 complete passive cycles and passed RESET qualification, exact ROM
reads, bus ownership, and terminal RESET-high/CLK-low/AD-high-Z gates.

The M33 never serviced a current V30 bus cycle. Its only role was policy work
between two separately qualified RESET epochs.

This is a bounded learned-stack replay proof. It does not claim general live
RAM, same-run write-to-read coherence, arbitrary stack depth, or repeated INT
calls whose saved IP differs on every invocation.

## Key physical evidence

| Evidence | Result |
|---|---:|
| Epoch A learned stack writes | `000C/F000/F046` PASS |
| IVT vector reads | `0018/F000` PASS |
| Handler diagnostic output | `I` PASS |
| Epoch B stack replay | PASS |
| Epoch B IRET checkpoint | 14 reads, PASS |
| Observer framing | 64/64 in both epochs |
| Current-cycle M33 | NONE |
| Terminal safe state | PASS in both epochs |

## Complete physical output

```text
[EPOCH-A LEARN V30 OUTPUT]
I
[SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
Current-address reads    PASS (16 hits)
Diagnostic I/O 00E9      PASS
Stack writes IP/CS/FLAGS PASS (000C/F000/F046)
IRET checkpoint          LEARN ONLY (1 reads)
Bus ownership/safety     PASS
C0C1-B2-A EPOCH-A LEARN RESULT      PASS

[ENGINEERING DETAILS]
PC1-C0C1-B2-A Bounded INT 10h Stack - 0.300 MHz
Epoch                    = EPOCH-A LEARN
Table shape              = 32 entries + sentinel
Execution budget         = 80 identical table blocks
Current-cycle M33        = NONE
RESET clock qualification= PASS
TX FIFO primed           = 4/4 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 64/64
Unsupported/high-Z cycles= 48
DMA remain pre/post      = 7756/1548
PIO1 OE pre/post         = 00000000/00000000
ROM image                = 30 bytes; SHA-256 c3e6e9adaf686b5c922bed2882051527d6569041d96caae82a88b46e2559588e
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
04 addr=F0000 addr_raw=01840322 data_raw=002CDD4B data=31FA hit=YES expected=31FA
05 addr=F0002 addr_raw=018C0322 data_raw=08661512 data=8EC0 hit=YES expected=8EC0
06 addr=F0004 addr_raw=01842322 data_raw=0826D51A data=BCD0 hit=YES expected=BCD0
07 addr=F0006 addr_raw=018C2322 data_raw=00260102 data=0800 hit=YES expected=0800
08 addr=F0008 addr_raw=01840362 data_raw=00269907 data=49B0 hit=YES expected=49B0
09 addr=F000A addr_raw=018C0362 data_raw=04247542 data=10CD hit=YES expected=10CD
10 addr=F000C addr_raw=01842362 data_raw=0C6E555F data=FEEB hit=YES expected=FEEB
11 addr=F000E addr_raw=018C2362 data_raw=0024D112 data=9090 hit=YES expected=9090
12 addr=00040 addr_raw=00000702 data_raw=00248142 data=0018 hit=YES expected=0018
13 addr=00042 addr_raw=00080702 data_raw=0024411E data=F000 hit=YES expected=F000
14 addr=007FE addr_raw=0848BFC3 data_raw=002865BE data=F046 hit=NO
15 addr=007FC addr_raw=0840BFC3 data_raw=002041BE data=F000 hit=NO
16 addr=007FA addr_raw=08489FC3 data_raw=002021E2 data=000C hit=NO
17 addr=F0018 addr_raw=01848362 data_raw=002E995F data=E9BA hit=YES expected=E9BA
18 addr=F001A addr_raw=018C8362 data_raw=0866011E data=EE00 hit=YES expected=EE00
19 addr=F001C addr_raw=0184A362 data_raw=042C7552 data=90CF hit=YES expected=90CF
20 addr=F001E addr_raw=018CA362 data_raw=002CA142 data=001E hit=NO
21 addr=000E9 addr_raw=040016C3 data_raw=00260886 data=4900 hit=NO
22 addr=F0020 addr_raw=01840323 data_raw=00240103 data=0020 hit=NO
23 addr=007FA addr_raw=08489F43 data_raw=08689D63 data=07FA hit=NO
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.

[EPOCH-B REPLAY V30 OUTPUT]
I
[SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
Current-address reads    PASS (57 hits)
Diagnostic I/O 00E9      PASS
Stack writes IP/CS/FLAGS PASS (000C/F000/F046)
IRET checkpoint          PASS (14 reads)
Bus ownership/safety     PASS
C0C1-B2-A EPOCH-B REPLAY RESULT      PASS

[ENGINEERING DETAILS]
PC1-C0C1-B2-A Bounded INT 10h Stack - 0.300 MHz
Epoch                    = EPOCH-B REPLAY
Table shape              = 32 entries + sentinel
Execution budget         = 80 identical table blocks
Current-cycle M33        = NONE
RESET clock qualification= PASS
TX FIFO primed           = 4/4 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 64/64
Unsupported/high-Z cycles= 7
DMA remain pre/post      = 7756/1548
PIO1 OE pre/post         = 00000000/00000000
ROM image                = 30 bytes; SHA-256 c3e6e9adaf686b5c922bed2882051527d6569041d96caae82a88b46e2559588e
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
04 addr=F0000 addr_raw=01840322 data_raw=002CDD4B data=31FA hit=YES expected=31FA
05 addr=F0002 addr_raw=018C0322 data_raw=08661512 data=8EC0 hit=YES expected=8EC0
06 addr=F0004 addr_raw=01842322 data_raw=0826D51A data=BCD0 hit=YES expected=BCD0
07 addr=F0006 addr_raw=018C2322 data_raw=00260102 data=0800 hit=YES expected=0800
08 addr=F0008 addr_raw=01840362 data_raw=00269907 data=49B0 hit=YES expected=49B0
09 addr=F000A addr_raw=018C0362 data_raw=04247542 data=10CD hit=YES expected=10CD
10 addr=F000C addr_raw=01842362 data_raw=0C6E555F data=FEEB hit=YES expected=FEEB
11 addr=F000E addr_raw=018C2362 data_raw=0024D112 data=9090 hit=YES expected=9090
12 addr=00040 addr_raw=00000702 data_raw=00248142 data=0018 hit=YES expected=0018
13 addr=00042 addr_raw=00080702 data_raw=0024411E data=F000 hit=YES expected=F000
14 addr=007FE addr_raw=0848BFC3 data_raw=002865BE data=F046 hit=NO
15 addr=007FC addr_raw=0840BFC3 data_raw=002041BE data=F000 hit=NO
16 addr=007FA addr_raw=08489FC3 data_raw=002021E2 data=000C hit=NO
17 addr=F0018 addr_raw=01848362 data_raw=002E995F data=E9BA hit=YES expected=E9BA
18 addr=F001A addr_raw=018C8362 data_raw=0866011E data=EE00 hit=YES expected=EE00
19 addr=F001C addr_raw=0184A362 data_raw=042C7552 data=90CF hit=YES expected=90CF
20 addr=F001E addr_raw=018CA362 data_raw=002CA142 data=001E hit=NO
21 addr=000E9 addr_raw=040016C3 data_raw=00260886 data=4900 hit=NO
22 addr=F0020 addr_raw=01840323 data_raw=00240103 data=0020 hit=NO
23 addr=007FA addr_raw=08489F43 data_raw=00202162 data=000C hit=YES expected=000C
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.

C0C1-B2-A OVERALL RESULT = PASS
```

## Next gate

The next RAM milestone must prove same-run write-to-read coherence rather than
learning across RESET. It should begin with one bounded RAM word or stack slot,
then expand only when every supported address has a deterministic no-wait hit
or a controllable READY path.
