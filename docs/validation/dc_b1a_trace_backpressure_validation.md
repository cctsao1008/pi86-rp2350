# DC-B1-A Authentic Trace Backpressure Physical Validation

- Date: 2026-08-22
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.300 MHz
- Target: `pc1c_dual_core_trace_backpressure`
- Firmware commit: `74145bb`
- UF2 size: 101,376 bytes
- UF2 SHA-256: `1561124e3310d67261c737de14da5e0e96845c34c4d242e1841b6508965aafd2`
- Result: **PASS**

## Accepted conclusion

DC-B1-A physically validates bounded transport of authentic PIO0/DMA GPIO
trace words between the realtime and service roles. With Core1 consuming, all
192 words arrived in order with zero drops. With Core1 deliberately stalled,
the 64-word ring retained exactly one full capacity and counted the remaining
128 producer attempts as non-blocking drops. After Core1 resumed, all 64
retained words drained in order.

The same run retained the accepted late-connect behavior: Core1 waited
8,396,095 microseconds and emitted the report from its first line. Epoch A,
Epoch B, 52/52 PIO-qualified pairs, DC-A, DC-B0, bus ownership, and terminal
safety all passed.

The transport stress occurs after the V30 reaches its safe terminal state,
using genuine retained trace content. This proves record ordering, bounded
capacity, overflow accounting, and recovery, but not yet simultaneous
long-running V30 execution plus CDC disconnect/backpressure. That remains
DC-B1-B.

## Key physical evidence

| Evidence | Result |
|---|---:|
| CDC output owner | Core1 PASS |
| Late CDC wait | 8,396,095 us |
| Epoch A / Epoch B | PASS / PASS |
| PIO-qualified pairs | 52/52 PASS |
| Active trace ordering | 192/192 PASS |
| Active trace drops | 0 PASS |
| Stalled consumer retained | 64/64 |
| Stalled consumer drops | 128/128 PASS |
| Post-stall ordered drain | 64/64 PASS |
| Producer wait on full ring | NONE PASS |
| DC-A / DC-B0 | PASS / PASS |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |

## Accepted physical output

```text
[DC-B0 SERVICE-CORE OUTPUT]
CDC output owner           = Core1 PASS
USB/CDC initialization     = Core0 SDK-required PASS
USB IRQ during V30 epochs  = MASKED PASS
V30 start requires CDC     = NO PASS
CDC connect wait on Core1  = 8396095 us
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

Raw records decoded        = 24/96

[DC-A DUAL-CORE FOUNDATION]
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

[DC-B1-A AUTHENTIC TRACE BACKPRESSURE]
Source records             = retained PIO0/DMA GPIO words
Active transport ordering  = PASS (192/192)
Active transport drops     = 0 PASS
Stalled consumer accepted  = 64/64
Stalled consumer drops     = 128/128 PASS
Post-stall drain ordering  = PASS (64/64)
Producer wait on full ring = NONE PASS
DC-B1-A RESULT             = PASS

DC-B0 SERVICE OUTPUT RESULT = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```

## Next gate

DC-B1-B must combine repeated or long-running V30 execution with live service
transport, terminal disconnect/reconnect, and deliberate output backpressure.
No service condition may change CPU-visible results, response deadlines, bus
ownership, or terminal safety.
