# PC1-C0C1-B2-C Multi-Slot / Byte-Lane RAM Validation

- Date: 2026-08-21
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_multi_slot_ram`
- Firmware commit: `6be3b2c`
- UF2 size: 94,720 bytes
- UF2 SHA-256: `78daf03475328ff6302bc7544d78d4bf506066e0c35bd72a740d2867673701de`
- V30 image: 44 bytes
- V30 image SHA-256: `bc6e4b642994f2a04d64d1df64d9d4e6725672b540155f5e4456459f64cc16d4`
- Result: **PASS**

## Accepted conclusion

The physical V30 completed two same-run epochs. Epoch A learned a finite
execution order while leaving RAM reads unsupported. Epoch B used an exact
PIO1 matcher and indexed PUTGET responder to retain and replay two independent
16-bit words plus ordered low- and high-byte transfers. No M33 participated in
a current V30 bus cycle.

The V30 wrote and consumed `1234h` at `00100h`, `5678h` at `00102h`,
a low-lane `34h` at `00104h`, and a high-lane `34h` at `00105h`.
It mirrored every read through port `00E8h`, proving CPU consumption rather
than only PIO drive. All 52 learned pairs qualified, both DMA streams and both
PIO TX FIFOs drained to zero, and the run ended RESET-high, CLK-low, AD-high-Z.

Byte-cycle validation considers only the selected physical lane. The inactive
lane is not part of an 8-bit transfer and may retain address-like values.
RP2350 indexed GET also consumes an additional TX/DREQ-path word in current
silicon; the accepted descriptor stream therefore contains four explicit
padding words, one after each indexed GET.

This gate proves bounded same-run multi-slot and byte-lane coherence on the
learned exact path. It does not claim arbitrary-address general RAM, an
unbounded miss path, or service-core participation in a current cycle.

## Key physical evidence

| Evidence | Epoch A | Epoch B |
|---|---:|---:|
| Word 0 `00100h` | write `1234h` PASS | write/read/OUT PASS |
| Word 1 `00102h` | write `5678h` PASS | write/read/OUT PASS |
| Low byte `00104h` | active lane `34h` PASS | write/read/OUT PASS |
| High byte `00105h` | active lane `34h` PASS | write/read/OUT PASS |
| Learned/qualified pairs | 52 learned | 52/52 PASS |
| Descriptor words/padding | 56/4 | 56/4 |
| Key/descriptor DMA remain | learning only | 0/0 PASS |
| Key/descriptor FIFO remain | 0/0 | 0/0 PASS |
| Observer framing | 96/96 | 96/96 |
| Current-cycle M33 | NONE | NONE |
| Terminal safe state | PASS | PASS |

## Complete physical output

```text
[EPOCH-A LEARN SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
ROM response data        PASS (44 reads)
WORD0 write 00100=1234   PASS (observed 1234)
WORD0 read  00100=1234   LEARN ONLY (observed 0100)
WORD0 OUT   00E8=1234    LEARN ONLY (observed 0100)
WORD1 write 00102=5678   PASS (observed 5678)
WORD1 read  00102=5678   LEARN ONLY (observed 0102)
WORD1 OUT   00E8=5678    LEARN ONLY (observed 0102)
LOW8  write 00104=0034   PASS (observed 0034)
LOW8  read  00104=0034   LEARN ONLY (observed 0004)
LOW8  OUT   00E8=0034    LEARN ONLY (observed 0004)
HIGH8 write 00105=3400   PASS (observed 3400)
HIGH8 read  00105=3400   LEARN ONLY (observed 0100)
HIGH8 OUT   00E8=0034    LEARN ONLY (observed 0001)
Checkpoint loop          PASS (20 reads)
Bus ownership/safety     PASS
C0C1-B2-C EPOCH-A LEARN RESULT      PASS

[ENGINEERING DETAILS]
PC1-C0C1-B2-C Multi-Slot / Byte-Lane RAM - 0.300 MHz
Epoch                    = EPOCH-A LEARN
Current-cycle M33        = NONE
PIO1 program             = bounded 32-entry ROM selector (32 words)
PIO-local RAM slots      = WORD0:1 WORD1:2 BYTE:3
Byte-cycle validation    = ACTIVE LANE ONLY
Indexed PUTGET storage   = NOT USED
Learned exact pairs      = 52
Descriptor words/padding = 56/4
PIO-qualified pairs      = 0/52
RESET clock qualification= PASS
TX FIFO primed           = 4/0 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 96/96
Non-ROM/other cycles     = 52
DMA remain key/desc      = 12412/0 -> 3100/0
FIFO remain key/desc     = 0/0
ROM image                = 44 bytes; SHA-256 bc6e4b642994f2a04d64d1df64d9d4e6725672b540155f5e4456459f64cc16d4
TERMINAL SAFE STATE      = PASS

[FIRST-CYCLE PHASE TRACE]
AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA

[PASSIVE ADDRESS / R2-DATA TRACE]
00 addr=FFFF0 type=MEMR data=00EA addr_raw=09C6DF3F data_raw=002C1543
01 addr=FFFF2 type=MEMR data=0000 addr_raw=09CEDF3F data_raw=00240102
02 addr=FFFF4 type=MEMR data=90F0 addr_raw=09C6FF3F data_raw=0024D513
03 addr=FFFF6 type=MEMR data=FFF6 addr_raw=09CEFF3F data_raw=086EFD1F
04 addr=F0000 type=MEMR data=BAFA addr_raw=01840322 data_raw=006ED55B
05 addr=F0002 type=MEMR data=00E8 addr_raw=018C0322 data_raw=00241543
06 addr=F0004 type=MEMR data=34B8 addr_raw=01842322 data_raw=0824D14B
07 addr=F0006 type=MEMR data=A312 addr_raw=018C2322 data_raw=006C891A
08 addr=F0008 type=MEMR data=0100 addr_raw=01840362 data_raw=00240902
09 addr=F000A type=MEMR data=78B8 addr_raw=018C0362 data_raw=0026D14F
10 addr=00100 type=MEMW data=1234 addr_raw=00000B82 data_raw=0064E1A3
11 addr=F000C type=MEMR data=A356 addr_raw=01842362 data_raw=006CAD1A
12 addr=F000E type=MEMR data=0102 addr_raw=018C2362 data_raw=002C0902
13 addr=F0010 type=MEMR data=02A1 addr_raw=01848322 data_raw=04641103
14 addr=00102 type=MEMW data=5678 addr_raw=00080B82 data_raw=0864C5E7
15 addr=F0012 type=MEMR data=EF01 addr_raw=018C8322 data_raw=0C66091E
16 addr=F0014 type=MEMR data=00A1 addr_raw=0184A322 data_raw=04241103
17 addr=00102 type=MEMR data=0102 addr_raw=00080B02 data_raw=002C0922
18 addr=F0016 type=MEMR data=EF01 addr_raw=018CA322 data_raw=0C66091E
19 addr=000E8 type=IOW data=0102 addr_raw=000016C3 data_raw=002C0882
20 addr=F0018 type=MEMR data=34B0 addr_raw=01848362 data_raw=0824D10B
21 addr=F001A type=MEMR data=04A2 addr_raw=018C8362 data_raw=082C1103
22 addr=00100 type=MEMR data=0100 addr_raw=00000B02 data_raw=00240922
23 addr=F001C type=MEMR data=A001 addr_raw=0184A362 data_raw=0424011A
24 addr=000E8 type=IOW data=0100 addr_raw=000016C3 data_raw=00240882
25 addr=F001E type=MEMR data=0104 addr_raw=018CA362 data_raw=00242902
26 addr=F0020 type=MEMR data=B0EE addr_raw=01840323 data_raw=002C755B
27 addr=00104 type=MEMW data=0134 addr_raw=02002B82 data_raw=0224A9A3
28 addr=F0022 type=MEMR data=A234 addr_raw=018C0323 data_raw=0064A11B
29 addr=00104 type=MEMR data=0104 addr_raw=02002B02 data_raw=02242922
30 addr=F0024 type=MEMR data=0105 addr_raw=01842323 data_raw=04242902
31 addr=000E8 type=IOW data=0104 addr_raw=020016C3 data_raw=02242882
32 addr=F0026 type=MEMR data=05A0 addr_raw=018C2323 data_raw=08241903
33 addr=F0028 type=MEMR data=EE01 addr_raw=01840363 data_raw=0C66011E
34 addr=00105 type=MEMW data=3401 addr_raw=04002B82 data_raw=0C2441AA
35 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
36 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
37 addr=00105 type=MEMR data=0105 addr_raw=04002B02 data_raw=04242922
38 addr=F002E type=MEMR data=002E addr_raw=018C2363 data_raw=002C2143
39 addr=000E8 type=IOW data=0101 addr_raw=020016C3 data_raw=06240882
40 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
41 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
42 addr=F002E type=MEMR data=002E addr_raw=018C2363 data_raw=002C2143
43 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
44 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
45 addr=F002E type=MEMR data=002E addr_raw=018C2363 data_raw=002C2143
46 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
47 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.

[EPOCH-B SAME-RUN SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
ROM response data        PASS (44 reads)
WORD0 write 00100=1234   PASS (observed 1234)
WORD0 read  00100=1234   PASS (observed 1234)
WORD0 OUT   00E8=1234    PASS (observed 1234)
WORD1 write 00102=5678   PASS (observed 5678)
WORD1 read  00102=5678   PASS (observed 5678)
WORD1 OUT   00E8=5678    PASS (observed 5678)
LOW8  write 00104=0034   PASS (observed 0034)
LOW8  read  00104=0034   PASS (observed 0034)
LOW8  OUT   00E8=0034    PASS (observed 0034)
HIGH8 write 00105=3400   PASS (observed 3400)
HIGH8 read  00105=3400   PASS (observed 3400)
HIGH8 OUT   00E8=0034    PASS (observed 0034)
Checkpoint loop          PASS (20 reads)
Bus ownership/safety     PASS
C0C1-B2-C EPOCH-B SAME-RUN RESULT      PASS

[ENGINEERING DETAILS]
PC1-C0C1-B2-C Multi-Slot / Byte-Lane RAM - 0.300 MHz
Epoch                    = EPOCH-B SAME-RUN
Current-cycle M33        = NONE
PIO1 program             = exact matcher + indexed PUTGET responder (8+24 words)
PIO-local RAM slots      = WORD0:1 WORD1:2 BYTE:3
Byte-cycle validation    = ACTIVE LANE ONLY
Indexed PUTGET storage   = PASS
Learned exact pairs      = 52
Descriptor words/padding = 56/4
PIO-qualified pairs      = 52/52
RESET clock qualification= PASS
TX FIFO primed           = 4/4 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 96/96
Non-ROM/other cycles     = 52
DMA remain key/desc      = 48/52 -> 0/0
FIFO remain key/desc     = 0/0
ROM image                = 44 bytes; SHA-256 bc6e4b642994f2a04d64d1df64d9d4e6725672b540155f5e4456459f64cc16d4
TERMINAL SAFE STATE      = PASS

[FIRST-CYCLE PHASE TRACE]
AF raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
R1 raw=09E6DD3F ASTB=0 CLK=1 AD=FFF0
F1 raw=01841523 ASTB=0 CLK=0 AD=00E0
R2 raw=002C1543 ASTB=0 CLK=1 AD=00EA
F2 raw=000C1543 ASTB=0 CLK=0 AD=00EA
R3 raw=002C1543 ASTB=0 CLK=1 AD=00EA

[PASSIVE ADDRESS / R2-DATA TRACE]
00 addr=FFFF0 type=MEMR data=00EA addr_raw=09C6DF3F data_raw=002C1543
01 addr=FFFF2 type=MEMR data=0000 addr_raw=09CEDF3F data_raw=00240102
02 addr=FFFF4 type=MEMR data=90F0 addr_raw=09C6FF3F data_raw=0024D513
03 addr=FFFF6 type=MEMR data=FFF6 addr_raw=09CEFF3F data_raw=086EFD1F
04 addr=F0000 type=MEMR data=BAFA addr_raw=01840322 data_raw=006ED55B
05 addr=F0002 type=MEMR data=00E8 addr_raw=018C0322 data_raw=00241543
06 addr=F0004 type=MEMR data=34B8 addr_raw=01842322 data_raw=0824D14B
07 addr=F0006 type=MEMR data=A312 addr_raw=018C2322 data_raw=006C891A
08 addr=F0008 type=MEMR data=0100 addr_raw=01840362 data_raw=00240902
09 addr=F000A type=MEMR data=78B8 addr_raw=018C0362 data_raw=0026D14F
10 addr=00100 type=MEMW data=1234 addr_raw=00000B82 data_raw=0064E1A3
11 addr=F000C type=MEMR data=A356 addr_raw=01842362 data_raw=006CAD1A
12 addr=F000E type=MEMR data=0102 addr_raw=018C2362 data_raw=002C0902
13 addr=F0010 type=MEMR data=02A1 addr_raw=01848322 data_raw=04641103
14 addr=00102 type=MEMW data=5678 addr_raw=00080B82 data_raw=0864C5E7
15 addr=F0012 type=MEMR data=EF01 addr_raw=018C8322 data_raw=0C66091E
16 addr=F0014 type=MEMR data=00A1 addr_raw=0184A322 data_raw=04241103
17 addr=00102 type=MEMR data=5678 addr_raw=00080B02 data_raw=0864C567
18 addr=F0016 type=MEMR data=EF01 addr_raw=018CA322 data_raw=0C66091E
19 addr=000E8 type=IOW data=5678 addr_raw=000016C3 data_raw=0864C4C7
20 addr=F0018 type=MEMR data=34B0 addr_raw=01848362 data_raw=0824D10B
21 addr=F001A type=MEMR data=04A2 addr_raw=018C8362 data_raw=082C1103
22 addr=00100 type=MEMR data=1234 addr_raw=00000B02 data_raw=0064E123
23 addr=F001C type=MEMR data=A001 addr_raw=0184A362 data_raw=0424011A
24 addr=000E8 type=IOW data=1234 addr_raw=000016C3 data_raw=0064E083
25 addr=F001E type=MEMR data=0104 addr_raw=018CA362 data_raw=00242902
26 addr=F0020 type=MEMR data=B0EE addr_raw=01840323 data_raw=002C755B
27 addr=00104 type=MEMW data=1234 addr_raw=02002B82 data_raw=0264E1A3
28 addr=F0022 type=MEMR data=A234 addr_raw=018C0323 data_raw=0064A11B
29 addr=00104 type=MEMR data=1234 addr_raw=02002B02 data_raw=0264E123
30 addr=F0024 type=MEMR data=0105 addr_raw=01842323 data_raw=04242902
31 addr=000E8 type=IOW data=1234 addr_raw=020016C3 data_raw=0264E083
32 addr=F0026 type=MEMR data=05A0 addr_raw=018C2323 data_raw=08241903
33 addr=F0028 type=MEMR data=EE01 addr_raw=01840363 data_raw=0C66011E
34 addr=00105 type=MEMW data=3412 addr_raw=04002B82 data_raw=082CC1AA
35 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
36 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
37 addr=00105 type=MEMR data=3412 addr_raw=04002B02 data_raw=082CC12A
38 addr=F002E type=MEMR data=002E addr_raw=018C2363 data_raw=002C2143
39 addr=000E8 type=IOW data=1234 addr_raw=020016C3 data_raw=0264E083
40 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
41 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
42 addr=F002E type=MEMR data=002E addr_raw=018C2363 data_raw=002C2143
43 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
44 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
45 addr=F002E type=MEMR data=002E addr_raw=018C2363 data_raw=002C2143
46 addr=F002A type=MEMR data=FEEB addr_raw=018C0363 data_raw=0C6E555F
47 addr=F002C type=MEMR data=002C addr_raw=01842363 data_raw=00242143
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.

C0C1-B2-C OVERALL RESULT = PASS
```

## Next gate

Keep `pc1c_multi_slot_ram` as a permanent bounded RAM regression. With B2-C
accepted, dual-core DC-A may now begin as strictly non-driving infrastructure:
bounded SPSC rings, heartbeat, saturation, overflow, and service-core-stall
isolation. PIO/DMA remains the only current-cycle V30 data plane.
