# DC-B0 Service-Core Output Physical Validation

- Date: 2026-08-22
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_dual_core_service_output`
- Firmware commit: `19a4291`
- UF2 size: 97,792 bytes
- UF2 SHA-256: `5006b25e0cc4b7744e03c03329236896ae51d1cf66fca1e363701f8f307760e9`
- Result: **PASS**

## Accepted conclusion

DC-B0 physically separates V30 execution from CDC connection and
human-readable trace processing. Pico SDK requires USB device initialization
on the default alarm-pool core, so Core0 performs that bounded pre-release
initialization. Core0 does not wait for a terminal, decode trace data, or
format output, and all interrupts are masked during both V30 execution epochs.

Core1 waited 6,189,335 microseconds for the CDC terminal. When the terminal was
opened, the retained report appeared from its first line with no missing
prefix. Core1 decoded fixed two-word PIO0/DMA address/data records from SRAM
and produced all CDC output. Both B2-C epochs, all 52 PIO-qualified pairs, the
complete DC-A isolation regression, and the terminal bus-safety state passed.

This accepts one-shot post-run late-connect output. It does not yet prove live
trace transport while the V30 continues to run, disconnect/reconnect during a
long-running workload, or deliberate CDC backpressure. Those are DC-B1.

## Key physical evidence

| Evidence | Result |
|---|---:|
| CDC output core | Core1 PASS |
| SDK-required USB initialization | Core0 PASS |
| USB IRQ during V30 epochs | masked PASS |
| V30 start dependency on CDC | none PASS |
| Late CDC connection wait | 6,189,335 us |
| Epoch A | PASS |
| Epoch B | PASS |
| PIO-qualified pairs | 52/52 PASS |
| Service-decoded raw records | 24/96 |
| DC-A isolation regression | PASS |
| B2-C regression | PASS |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |

## Complete physical output

```text
[DC-B0 SERVICE-CORE OUTPUT]
CDC output owner           = Core1 PASS
USB/CDC initialization     = Core0 SDK-required PASS
USB IRQ during V30 epochs  = MASKED PASS
V30 start requires CDC     = NO PASS
CDC connect wait on Core1  = 6189335 us
Realtime printf/formatting = NONE PASS
Raw trace ABI              = 2 x uint32_t PASS
Trace ownership transfer   = PIO0/DMA -> SRAM -> Core1 PASS

[EPOCH-A SERVICE SUMMARY]
Measurement/reset/fetch   = PASS
ROM execution/data        = PASS (44 reads)
RAM write mask            = 0F PASS
RAM read / OUT masks      = 00 / 00 LEARN ONLY
PIO-qualified pairs       = 0/52 LEARN ONLY
DMA/FIFO terminal residue = 3100/0/0/0 LEARN
Terminal safe state       = PASS
EPOCH-A RESULT             = PASS

[EPOCH-B SERVICE SUMMARY]
Measurement/reset/fetch   = PASS
ROM execution/data        = PASS (44 reads)
RAM write mask            = 0F PASS
RAM read / OUT masks      = 0F / 0F PASS
PIO-qualified pairs       = 52/52 PASS
DMA/FIFO terminal residue = 0/0/0/0 PASS
Terminal safe state       = PASS
EPOCH-B RESULT             = PASS

[SERVICE-DECODED RAW TRACE]
00 addr=FFFF0 addr_raw=09C6DF3F data_raw=002C1543 data=00EA
01 addr=FFFF2 addr_raw=09CEDF3F data_raw=00240102 data=0000
02 addr=FFFF4 addr_raw=09C6FF3F data_raw=0024D513 data=90F0
03 addr=FFFF6 addr_raw=09CEFF3F data_raw=086EFD1F data=FFF6
04 addr=F0000 addr_raw=01840322 data_raw=006ED55B data=BAFA
05 addr=F0002 addr_raw=018C0322 data_raw=00241543 data=00E8
06 addr=F0004 addr_raw=01842322 data_raw=0824D14B data=34B8
07 addr=F0006 addr_raw=018C2322 data_raw=006C891A data=A312
08 addr=F0008 addr_raw=01840362 data_raw=00240902 data=0100
09 addr=F000A addr_raw=018C0362 data_raw=0026D14F data=78B8
10 addr=00100 addr_raw=00000B82 data_raw=0064E1A3 data=1234
11 addr=F000C addr_raw=01842362 data_raw=006CAD1A data=A356
12 addr=F000E addr_raw=018C2362 data_raw=002C0902 data=0102
13 addr=F0010 addr_raw=01848322 data_raw=04641103 data=02A1
14 addr=00102 addr_raw=00080B82 data_raw=0864C5E7 data=5678
15 addr=F0012 addr_raw=018C8322 data_raw=0C66091E data=EF01
16 addr=F0014 addr_raw=0184A322 data_raw=04241103 data=00A1
17 addr=00102 addr_raw=00080B02 data_raw=0864C567 data=5678
18 addr=F0016 addr_raw=018CA322 data_raw=0C66091E data=EF01
19 addr=000E8 addr_raw=000016C3 data_raw=0864C4C7 data=5678
20 addr=F0018 addr_raw=01848362 data_raw=0824D10B data=34B0
21 addr=F001A addr_raw=018C8362 data_raw=082C1103 data=04A2
22 addr=00100 addr_raw=00000B02 data_raw=0064E123 data=1234
23 addr=F001C addr_raw=0184A362 data_raw=0424011A data=A001
Raw records decoded        = 24/96

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

DC-B0 SERVICE OUTPUT RESULT = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Next gate

DC-B1 must exercise live bounded raw-record transport while the V30 continues
to run. A deliberately stalled or disconnected service consumer must cause
counted non-blocking drops only. CDC reconnect and backpressure must not alter
V30-visible traces, response deadlines, bus ownership, or terminal safety.
