# PC1-C0C0-H Native BIOS HELLO RP2350 Validation

- Date: 2026-08-19
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_native_bios_hello`
- Firmware commit: `3093194`
- UF2 size: 91,648 bytes
- UF2 SHA-256: `D6412E4C8C2BFCA91C396D6ED5718DB22871AE3140118F395F80C212816A9D2F`
- ROM size: 48 bytes
- ROM SHA-256: `EA43AFA582768866943C0B938B2F83A18D9D80B2AD487C8D995B2CB3FC17D111`
- Input synchronizers: SDK defaults
- Result: **PASS**

## Purpose

Prove the first complete Native BIOS diagnostic chain on the physical V30:

```text
FFFF0 reset fetch
  -> far jump to F0000
  -> execute RP2350 internal-SRAM ROM
  -> fourteen V30 OUT cycles to 00E9h
  -> passive PIO0/DMA capture
  -> exact HELLO RP2350 CR LF reconstruction
  -> repeatable JMP $ checkpoint
```

This extends the accepted descriptor-fed C0C0 engine. It proves real V30 I/O
execution but does not claim arbitrary-address C0C1 ROM service.

## CPU-visible evidence

| Evidence | Physical result | Meaning |
|---|---:|---|
| First reset fetch | `FFFF0`, word memory read | clean post-reset epoch |
| First response | `00EA` at R2/F2/R3 | stable far-jump opcode |
| Far-jump target | `F0000` | reset vector consumed by the V30 |
| Diagnostic writes | 14/14 at `00E9h` | complete CPU-to-companion message |
| Reconstructed output | `HELLO RP2350\r\n` | exact byte order and content |
| Odd-port lane | `IOW / HIGH` | correct V30 byte-lane behavior |
| ROM responses | 30/30 qualified pairs | complete descriptor-fed program |
| Deadline misses | 0 | every supported read met its window |
| Unqualified drives | 0 | no invalid AD ownership |
| Checkpoint | four reads at `F002E`, data `FEEB` | repeatable final `JMP $` |
| Terminal state | RESET high, CLK low, AD high-Z | safe shutdown |

The trace records `4800h`, `4500h`, `4C00h`, and subsequent payloads on the
high lane because diagnostic port `00E9h` is odd. These are the physical ASCII
bytes for `H`, `E`, `L`, and the rest of the message.

## Complete physical output

```text
PC1-C0C0-H Descriptor-Fed Native BIOS Hello - 0.300 MHz
RESET qualification : clock-only; matcher/responder SMs disabled
Measurement epoch   : arm after RESET clocks with CLK stopped LOW
Realtime engine     : PIO1 synchronized CLK + exact matcher + AD/PINDIRS
Response policy     : SRAM key/descriptor tables -> DMA -> PIO1 FIFOs
Current-cycle M33   : NONE
Observer path       : passive PIO0 address/R2-data -> DMA -> SRAM
ROM image           : 48 bytes at F0000; SHA-256 ea43afa582768866943c0b938b2f83a18d9d80b2ad487c8d995b2cb3fc17d111
Input synchronizers : SDK defaults

RESET clock count         = PASS
PRE-RESET EVENT LEAK      = NO
PIO1 pre-release OE       = 00200000 CLK-ONLY PASS
FIRST post-reset address  = FFFF0 PASS
FIRST cycle type          = MEMORY READ PASS
Matcher FIFO primed       = 4/4 PASS
Responder FIFO primed     = 4/4 PASS
PRE-RELEASE DMA remain    = key 26 response 26
DMA observer first address= FFFF0 PASS
FIRST response R2/F2/R3   = 00EA PASS
Far-jump target observed  = F0000 PASS
Diagnostic port 00E9 bytes= 14/14 PASS
V30 diagnostic output     = "HELLO RP2350\r\n"
Checkpoint F002E reads   = 4/4 PASS
PIO-qualified pairs       = 30/30 PASS
POST-RUN DMA remain       = key 0 response 0 PASS
Matcher FIFO remain       = 0 PASS
Responder FIFO remain     = 0 PASS
Observer complete cycles  = 51
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
05  F0002  MEMR  WORD 018C0322 04241543 00E9 YES
06  F0004  MEMR  WORD 01842322 00269107 48B0 YES
07  F0006  MEMR  WORD 018C2322 002C755B B0EE YES
08  F0008  MEMR  WORD 01840362 0C66251E EE45 YES
09  000E9  IOW   HIGH 040016C3 00260086 4800 NO
10  F000A  MEMR  WORD 018C0362 08269107 4CB0 YES
11  F000C  MEMR  WORD 01842362 002C755B B0EE YES
12  000E9  IOW   HIGH 040016C3 08240886 4500 NO
13  F000E  MEMR  WORD 018C2362 0866255E EE4C YES
14  F0010  MEMR  WORD 01848322 08669907 4FB0 YES
15  000E9  IOW   HIGH 040016C3 08260086 4C00 NO
16  F0012  MEMR  WORD 018C8322 002C755B B0EE YES
17  F0014  MEMR  WORD 0184A322 0866011F EE20 YES
18  000E9  IOW   HIGH 040016C3 08260086 4C00 NO
19  F0016  MEMR  WORD 018CA322 0064D107 52B0 YES
20  000E9  IOW   HIGH 040016C3 08660886 4F00 NO
21  F0018  MEMR  WORD 01848362 002C755B B0EE YES
22  F001A  MEMR  WORD 018C8362 0866851E EE50 YES
23  000E9  IOW   HIGH 040016C3 0024008A 2000 NO
24  F001C  MEMR  WORD 0184A362 0064D10B 32B0 YES
25  000E9  IOW   HIGH 040016C3 00644086 5200 NO
26  F001E  MEMR  WORD 018CA362 002C755B B0EE YES
27  F0020  MEMR  WORD 01840323 0C6E811F EE33 YES
28  000E9  IOW   HIGH 040016C3 00244086 5000 NO
29  F0022  MEMR  WORD 018C0323 0824D90B 35B0 YES
30  000E9  IOW   HIGH 040016C3 0064408A 3200 NO
31  F0024  MEMR  WORD 01842323 002C755B B0EE YES
32  F0026  MEMR  WORD 018C2323 0866811F EE30 YES
33  000E9  IOW   HIGH 040016C3 0064488A 3300 NO
34  F0028  MEMR  WORD 01840363 08269903 0DB0 YES
35  000E9  IOW   HIGH 040016C3 0824488A 3500 NO
36  F002A  MEMR  WORD 018C0363 002C755B B0EE YES
37  F002C  MEMR  WORD 01842363 086E015E EE0A YES
38  000E9  IOW   HIGH 040016C3 0024408A 3000 NO
39  F002E  MEMR  WORD 018C2363 0C6E555F FEEB YES
... 11 additional cycles retained in SRAM

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

## Architecture conclusion

The physical V30 can execute a Native BIOS image served through the
descriptor-fed internal-SRAM/PIO/DMA engine and communicate with the RP2350
through genuine V30 I/O-write cycles. Diagnostic port `0xE9` is now the
accepted early Native BIOS console contract.

The descriptor-fed engine remains a permanent reproducible regression while
PC1-C0C1 develops current-address-indexed ROM service. Dynamic misses and slow
ROM/RAM/peripheral paths require the controllable READY capability specified
for the V3.0 HAT.
