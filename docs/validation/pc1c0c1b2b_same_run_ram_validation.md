# PC1-C0C1-B2-B Same-Run One-Slot RAM Validation

- Date: 2026-08-20
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_same_run_ram`
- Firmware commit: `ecd3ec8`
- UF2 size: 92,672 bytes
- UF2 SHA-256: `867aa140f85b83831a0102255a5e87c2de5593263c54262abd72f057ff208752`
- V30 image: 18 bytes
- V30 image SHA-256: `71d75ed6b761eeee399aa2e5d7037280e7500623d39d374ba0d3e9664bc1550e`
- Result: **PASS**

## Accepted conclusion

The physical V30 wrote `1234h` to `00100h`, later read `1234h` from the same
address, and then wrote the consumed value to `00102h` in the same execution
epoch. PIO1 captured the first write directly at R2, retained the raw
scattered-GPIO word in ISR, and replayed it for the read. The M33 performed no
current-cycle lookup or data transfer.

Epoch A learned only the finite execution order. Its RAM read remained high-Z
and produced `0100h`, which the V30 mirrored to `00102h`. Epoch B therefore
cannot be explained by a precompiled `1234h` response or by cross-RESET data
learning. It consumed all 30 learned key/descriptor pairs, completed both DMA
streams, retained 64 complete passive cycles, and ended RESET-high, CLK-low,
and AD-high-Z.

This proves same-run write-to-read coherence for one PIO-local word on a
learned bounded path. It does not claim general arbitrary-address RAM,
multiple live words, byte-lane coherence, or dynamic miss handling.

## Key physical evidence

| Evidence | Epoch A | Epoch B |
|---|---:|---:|
| Write `00100h` | `1234h` PASS | `1234h` PASS |
| Read `00100h` | `0100h`, unsupported | `1234h` PASS |
| V30 mirror write `00102h` | `0100h`, unsupported | `1234h` PASS |
| Learned/qualified pairs | 30 learned | 30/30 PASS |
| Key/descriptor DMA | not applicable | 0/0 remain PASS |
| Observer framing | 64/64 | 64/64 |
| Checkpoint reads | 17 PASS | 17 PASS |
| Current-cycle M33 | NONE | NONE |
| Terminal safe state | PASS | PASS |

## Complete physical output

```text
[EPOCH-A LEARN SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
ROM response data        PASS (28 reads)
Write 00100=1234         PASS (observed 1234)
Read  00100=1234         LEARN ONLY (observed 0100)
Write 00102=1234         LEARN ONLY (observed 0100)
Checkpoint loop          PASS (17 reads)
Bus ownership/safety     PASS
C0C1-B2-B EPOCH-A LEARN RESULT      PASS

[ENGINEERING DETAILS]
PC1-C0C1-B2-B Same-Run One-Slot RAM - 0.300 MHz
Epoch                    = EPOCH-A LEARN
Current-cycle M33        = NONE
PIO1 program             = bounded 32-entry ROM selector (32 words)
Learned exact pairs      = 30
PIO-qualified pairs      = 0/30
RESET clock qualification= PASS
TX FIFO primed           = 4/0 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 64/64
Unsupported/high-Z cycles= 36
DMA remain key/desc      = 7756/0 -> 1548/0
ROM image                = 18 bytes; SHA-256 71d75ed6b761eeee399aa2e5d7037280e7500623d39d374ba0d3e9664bc1550e
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
04 addr=F0000 type=MEMR data=B8FA addr_raw=01840322 data_raw=002ED55B
05 addr=F0002 type=MEMR data=1234 addr_raw=018C0322 data_raw=0064E103
06 addr=F0004 type=MEMR data=00A3 addr_raw=01842322 data_raw=042C1103
07 addr=F0006 type=MEMR data=8B01 addr_raw=018C2322 data_raw=04660912
08 addr=F0008 type=MEMR data=001E addr_raw=01840362 data_raw=002CA142
09 addr=00100 type=MEMW data=1234 addr_raw=00000B82 data_raw=0064E1A3
10 addr=F000A type=MEMR data=8901 addr_raw=018C0362 data_raw=04260912
11 addr=F000C type=MEMR data=021E addr_raw=01842362 data_raw=006CA142
12 addr=00100 type=MEMR data=0100 addr_raw=00000B02 data_raw=00240922
13 addr=F000E type=MEMR data=9001 addr_raw=018C2362 data_raw=04244112
14 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
15 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
16 addr=00102 type=MEMW data=0100 addr_raw=00080B82 data_raw=002409A2
17 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
18 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
19 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
20 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
21 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
22 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
23 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
24 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
25 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
26 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
27 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
28 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
29 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
30 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
31 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.

[EPOCH-B SAME-RUN SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
ROM response data        PASS (28 reads)
Write 00100=1234         PASS (observed 1234)
Read  00100=1234         PASS (observed 1234)
Write 00102=1234         PASS (observed 1234)
Checkpoint loop          PASS (17 reads)
Bus ownership/safety     PASS
C0C1-B2-B EPOCH-B SAME-RUN RESULT      PASS

[ENGINEERING DETAILS]
PC1-C0C1-B2-B Same-Run One-Slot RAM - 0.300 MHz
Epoch                    = EPOCH-B SAME-RUN
Current-cycle M33        = NONE
PIO1 program             = exact matcher + capture/replay responder (9+23 words)
Learned exact pairs      = 30
PIO-qualified pairs      = 30/30
RESET clock qualification= PASS
TX FIFO primed           = 4/4 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 64/64
Unsupported/high-Z cycles= 36
DMA remain key/desc      = 26/26 -> 0/0
ROM image                = 18 bytes; SHA-256 71d75ed6b761eeee399aa2e5d7037280e7500623d39d374ba0d3e9664bc1550e
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
04 addr=F0000 type=MEMR data=B8FA addr_raw=01840322 data_raw=002ED55B
05 addr=F0002 type=MEMR data=1234 addr_raw=018C0322 data_raw=0064E103
06 addr=F0004 type=MEMR data=00A3 addr_raw=01842322 data_raw=042C1103
07 addr=F0006 type=MEMR data=8B01 addr_raw=018C2322 data_raw=04660912
08 addr=F0008 type=MEMR data=001E addr_raw=01840362 data_raw=002CA142
09 addr=00100 type=MEMW data=1234 addr_raw=00000B82 data_raw=0064E1A3
10 addr=F000A type=MEMR data=8901 addr_raw=018C0362 data_raw=04260912
11 addr=F000C type=MEMR data=021E addr_raw=01842362 data_raw=006CA142
12 addr=00100 type=MEMR data=1234 addr_raw=00000B02 data_raw=0064E123
13 addr=F000E type=MEMR data=9001 addr_raw=018C2362 data_raw=04244112
14 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
15 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
16 addr=00102 type=MEMW data=1234 addr_raw=00080B82 data_raw=0064E1A3
17 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
18 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
19 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
20 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
21 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
22 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
23 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
24 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
25 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
26 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
27 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
28 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
29 addr=F0014 type=MEMR data=0014 addr_raw=0184A322 data_raw=0024A102
30 addr=F0010 type=MEMR data=FEEB addr_raw=01848322 data_raw=0C6E555F
31 addr=F0012 type=MEMR data=0012 addr_raw=018C8322 data_raw=002C8102
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.

C0C1-B2-B OVERALL RESULT = PASS
```

## Next gate

Keep this target as the one-slot coherence regression. The next RAM experiment
should add at least two independently addressed live words and byte-lane
coverage before reusing the mechanism for a stack. General RAM and slow or
unbounded lookup still require a deterministic hit architecture or
controllable READY on the V3.0 HAT.
