# AI-B0 Physical V30 Mailbox Validation

- Date: 2026-08-23
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.200 MHz
- Target: `ai_bridge_mailbox_200khz`
- Firmware commit: `fe273b4`
- UF2 size: 89,600 bytes
- UF2 SHA-256: `5b90cd7b6176cb50c97aa91883cd0f8fc20e4fce08f1e4039c1e54fe73ae622c`
- Native ROM size: 78 bytes
- Native ROM SHA-256: `908f4eb1e6020934977988958f31589773608b819260625e0606fc6b92d1b19f`
- Result: **PASS**

## Accepted conclusion

AI-B0 physically validates the first scripted host-to-V30-to-host message
exchange through the pi86-rp2350 mailbox boundary. The RP2350 internal-SRAM
response table staged `HELLO NEC V30`. Native, stack-free V30 code consumed all
seven aligned words, validated their XOR, and published
`HELLO OPENAI CODEX` through nine word-I/O writes at mailbox TX port `00E2h`.
It then issued a commit at `00E6h` and entered its aligned checkpoint loop.

PIO1 performed exact current-address selection and controlled scattered AD
GPIO/PINDIRS. DMA supplied repeated table blocks from internal SRAM. The M33
was absent from every current-cycle response. PIO0/DMA independently retained
96 complete observer cycles.

All 77 supported current-address reads returned the expected data with zero
mismatches. There were no odd-address ROM fetches. The reset vector, first
response, far target, mailbox payload, mailbox commit, four checkpoint reads,
and terminal RESET-high/CLK-low/AD-high-Z state all passed.

This accepts the physical scripted AI-B0 mailbox gate. It does not yet accept
runtime Core1-to-Core0 message staging, an asynchronous V30-visible RX/status
mailbox, USB HID, a Python-to-hardware transport, or the Codex adapter. Those
remain AI-B1 through AI-B3.

## Measured 0.300 MHz selector boundary

The retained `ai_bridge_mailbox` target was also exercised at 0.300 MHz with
the same 78-byte ROM, 42-entry table, default input synchronizers, and bus
ownership. It completed reset and native execution but reported three response
mismatches. The first was:

```text
First mismatch = F0036 expected 3356 observed 0036
```

`F0036` is the sixth staged greeting word and lies near the 31st linear-search
position. Earlier retained responses were coherent, with zero odd ROM fetches.
The result establishes a response-drive timing boundary for this linear
selector; it is not classified as a logic, reset, or ownership failure. The
0.300 MHz target remains available as a reproducible characterization build.

## Key physical evidence

| Evidence | Physical result |
|---|---:|
| RESET qualification | PASS |
| First post-reset fetch / response | `FFFF0` / `00EA` PASS |
| Native ROM execution target | `F0000` PASS |
| Supported current-address reads | 77, zero mismatches |
| Odd-address ROM fetches | 0 PASS |
| V30 mailbox output | `HELLO OPENAI CODEX` |
| Mailbox TX `00E2h` | PASS |
| Mailbox commit `00E6h` | PASS |
| Checkpoint reads | 4 PASS |
| Current-cycle M33 | NONE |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |

## Complete physical output

```text
[V30 MAILBOX OUTPUT]
HELLO OPENAI CODEX
[SUMMARY]
Measurement epoch        PASS
Reset / FFFF0 fetch      PASS
First response 00EA      PASS
F0000 ROM execution      PASS
Current-address reads    PASS (77 hits, 0 mismatches)
Mailbox TX I/O 00E2      PASS
Mailbox commit I/O 00E6  PASS
Aligned ROM fetches      PASS (0 odd)
Checkpoint loop          PASS (4 reads)
Bus ownership/safety     PASS
AI-B0 RESULT           PASS

[ENGINEERING DETAILS]
AI-B0 Physical V30 Mailbox Greeting - 0.200 MHz
Table shape              = 42 entries + sentinel
Execution budget         = 128 identical table blocks
Current-cycle M33        = NONE
RESET clock qualification= PASS
TX FIFO primed           = 4/4 PASS
PIO1 pre-release OE      = 00000000 PASS
PIO2 pre-release OE      = 00200000 CLK-ONLY PASS
CLK stopped LOW          = PASS
Observer complete cycles = 96/96
Unsupported/high-Z cycles= 19
Odd-address ROM fetches  = 0 PASS
Response data mismatches = 0 PASS
DMA remain pre/post      = 16252/4060
PIO1 OE pre/post         = 00000000/00000000
ROM image                = 78 bytes; SHA-256 908f4eb1e6020934977988958f31589773608b819260625e0606fc6b92d1b19f
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
05 addr=F0002 addr_raw=018C0322 data_raw=00263156 data=C88C hit=YES expected=C88C
06 addr=F0004 addr_raw=01842322 data_raw=002E7156 data=D88E hit=YES expected=D88E
07 addr=F0006 addr_raw=018C2322 data_raw=082EB14B data=2CBE hit=YES expected=2CBE
08 addr=F0008 addr_raw=01840362 data_raw=0026491A data=B900 hit=YES expected=B900
09 addr=F000A addr_raw=018C0362 data_raw=042C2102 data=0007 hit=YES expected=0007
10 addr=F000C addr_raw=01842362 data_raw=042C914B data=20BB hit=YES expected=20BB
11 addr=F000E addr_raw=018C2362 data_raw=042CE553 data=907F hit=YES expected=907F
12 addr=F0010 addr_raw=01848322 data_raw=0424794B data=31AD hit=YES expected=31AD
13 addr=F0012 addr_raw=018C8322 data_raw=046C151E data=E2C3 hit=YES expected=E2C3
14 addr=F0014 addr_raw=0184A322 data_raw=042E9D43 data=09FB hit=YES expected=09FB
15 addr=F002C addr_raw=01842363 data_raw=08240D66 data=4548 hit=YES expected=4548
16 addr=F0016 addr_raw=018CA322 data_raw=0C2CDD4E data=75DB hit=YES expected=75DB
17 addr=F0018 addr_raw=01848362 data_raw=0C6EC11B data=BE33 hit=YES expected=BE33
18 addr=F0010 addr_raw=01848322 data_raw=0424794B data=31AD hit=YES expected=31AD
19 addr=F0012 addr_raw=018C8322 data_raw=046C151E data=E2C3 hit=YES expected=E2C3
20 addr=F0014 addr_raw=0184A322 data_raw=042E9D43 data=09FB hit=YES expected=09FB
21 addr=F002E addr_raw=018C2363 data_raw=08262566 data=4C4C hit=YES expected=4C4C
22 addr=F0016 addr_raw=018CA322 data_raw=0C2CDD4E data=75DB hit=YES expected=75DB
23 addr=F0018 addr_raw=01848362 data_raw=0C6EC11B data=BE33 hit=YES expected=BE33
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Next gate

AI-B1 replaces the build-time greeting with a runtime-staged complete message.
Core1 receives a provider-neutral 64-byte record, transfers ownership through
a bounded SPSC queue, and Core0 publishes the V30-visible mailbox only after
the complete payload is locally staged. AI-B0 remains a permanent physical
mailbox and selector regression.
