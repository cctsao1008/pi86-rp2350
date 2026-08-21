# DC-A Dual-Core Foundation Physical Validation

- Date: 2026-08-21
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_dual_core_foundation`
- Firmware commit: `5de4874`
- UF2 size: 100,864 bytes
- UF2 SHA-256: `10b6ec9561bc81191c86eed6729d727b434a59b0ce93c9b329b880a273c0ce9d`
- Retained B2-C UF2 SHA-256: `78daf03475328ff6302bc7544d78d4bf506066e0c35bd72a740d2867673701de`
- Result: **PASS**

## Accepted conclusion

DC-A establishes the first physical dual-core isolation boundary without
changing the accepted V30 data plane. Core0 provisionally retained realtime
ownership; Core1 ran only a bounded shared-SRAM service role and never touched
GPIO, PIO, DMA, RESET, or CDC.

The physical test proved both directions of isolation. During Epoch A the
service heartbeat continued while the V30 executed. During Epoch B Core1 was
deliberately stalled for the entire accepted B2-C regression, yet the V30 still
completed all word and byte-lane RAM checks, all 52 PIO-qualified pairs, and the
terminal safety sequence. Core1 then resumed and its heartbeat restarted.

The two bounded SPSC channels preserved 64/64 trace words and 32/32 command
words in order. Sixteen producer attempts made while the trace ring was full
were counted and dropped without blocking. Thus service activity, service
backpressure, and a stopped service core are not synchronous dependencies of a
current V30 bus cycle.

This validates the logical realtime/service separation and the provisional
Core0=realtime, Core1=service placement at the 0.300 MHz baseline. It does not
yet validate USB CDC disconnect/backpressure or move trace decoding and output
off the realtime role; those remain DC-B acceptance work.

## Key physical evidence

| Evidence | Result |
|---|---:|
| Existing B2-C regression | PASS |
| PIO-qualified pairs | 52/52 PASS |
| Dual-core startup | PASS |
| Service heartbeat during Epoch A | PASS |
| Realtime-to-service trace ring | 64/64 ordered |
| Service-to-realtime command ring | 32/32 ordered |
| Full-ring behavior | 16 counted, non-blocking drops |
| Core1 stall throughout Epoch B | isolated PASS |
| Core1 resume and heartbeat | PASS |
| Bus ownership/safety | PASS |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |

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

[DC-A DUAL-CORE FOUNDATION]
Core placement             = Core0 realtime / Core1 service PROVISIONAL
Dual-core startup          = PASS
Service-core heartbeat     = PASS
Trace ring ordering        = PASS (64/64)
Command ring ordering      = PASS (32/32)
Queue overflow nonblocking = PASS (16 drops)
Service-core stall isolated= PASS
Service-core resume        = PASS
PC1-C B2-C regression      = PASS
Bus ownership/safety       = PASS
DC-A RESULT                = PASS
```

## Next gate

DC-B moves raw trace decode, formatting, and USB CDC output to the service role.
The realtime role must publish fixed-size records only and must remain
independent of CDC connection state or backpressure. The accepted B2-C and DC-A
checks remain mandatory regressions.
