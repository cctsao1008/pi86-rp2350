# AI-B1-A Runtime Mailbox 0.600 MHz Validation

- Date: 2026-08-23
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.600 MHz
- Target: `ai_bridge_runtime_mailbox_600khz`
- Firmware commit before evidence amendment: `7afe64e`
- UF2 size: 94,208 bytes
- UF2 SHA-256: `42621f316d992b2b42d2d843986a610c29df78ce98ce7497228f86108e594a5a`
- Native ROM size: 84 bytes
- Native ROM SHA-256: `6c203b2082b24d12a95b0443e63dc4ba65faeef5985c48e536af7d08e73e70af`
- Response architecture: **fixed / prestaged greeting with input-dependent XOR witness**
- Result: **PASS**

## Accepted conclusion

AI-B1-A physically validates the bounded runtime-staged dual-core mailbox at
0.600 MHz, twice the 0.300 MHz original-system baseline used by this project.
Core1 transferred one complete provider-neutral 64-byte record. Core0 accepted
and copied that complete record before publishing immutable PIO/DMA response
streams; no partial message became V30-visible.

PIO1 ran two independent relative-IRQ matcher/responder pairs from one 24-word
program image. SM0/SM1 supplied the bounded ROM sequence and SM2/SM3 supplied
the fixed `00E4h` mailbox response sequence. PIO2 independently owned V30 CLK,
while PIO0/DMA retained 74 passive physical cycles.

The physical V30 read all seven mailbox words from `00E4h`, produced a zero
XOR witness at `00E8h`, wrote `HELLO OPENAI CODEX` through `00E2h`, committed
the message through `00E6h`, and reached the ROM checkpoint. All 48 ROM pairs
and all seven mailbox pairs completed. Both response DMA streams drained with
zero deadline misses. The run ended RESET-high, CLK-low, and AD-high-Z.

This accepts AI-B1-A. It does not yet accept live STATUS publication while the
V30 is already polling, sustained multi-record exchange, USB HID transport, or
the Codex adapter; those remain AI-B1-B, AI-B1-C, AI-B2, and AI-B3.

## Key physical evidence

| Evidence | Physical result |
|---|---:|
| Configured V30 clock | 0.600 MHz |
| Reset vector / first response | `FFFF0` / `00EA` PASS |
| Core1 complete record | PASS |
| Core0 immutable staging | PASS |
| Mailbox RX `00E4h` | 7/7 words PASS |
| V30 input XOR `00E8h` | PASS |
| Mailbox TX `00E2h` | `HELLO OPENAI CODEX` PASS |
| Mailbox commit `00E6h` | PASS |
| ROM/mailbox key collisions | 0 PASS |
| ROM qualified pairs | 48/48 PASS |
| Mailbox qualified pairs | 7/7 PASS |
| Response deadline misses | 0 PASS |
| Current-cycle M33 | NONE |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |

## Complete physical output

```text
[V30 MAILBOX OUTPUT]
HELLO OPENAI CODEX

[SUMMARY]
Measurement epoch          PASS
Reset / FFFF0 fetch        PASS
First response 00EA        PASS
F0000 ROM execution        PASS
Core1 complete record      PASS
Core0 immutable staging    PASS
Mailbox RX I/O 00E4        PASS (7/7 words)
V30 input XOR at 00E8      PASS
Mailbox TX I/O 00E2        PASS
Mailbox commit I/O 00E6    PASS
ROM/mailbox key collisions 0 PASS
Current-cycle M33          NONE
Bus ownership/safety       PASS
AI-B1-A RESULT             PASS

[ENGINEERING DETAILS]
AI-B1-A Dual-Sequence Runtime Mailbox - 0.600 MHz
PIO1 allocation            = SM0/1 ROM, SM2/3 mailbox
PIO instruction words      = 11 + 13 = 24/32
PIO1 pre-release OE        = 00000000 PASS
PIO2 pre-release OE        = 00200000 CLK-ONLY PASS
ROM qualified pairs        = 48/48 PASS
Mailbox qualified pairs    = 7/7 PASS
ROM DMA remain key/response= 0/0
Mailbox DMA remain key/resp= 0/0
Response deadline misses   = 0 PASS
Observer complete cycles   = 74
ROM image                  = 84 bytes; SHA-256 6c203b2082b24d12a95b0443e63dc4ba65faeef5985c48e536af7d08e73e70af
TERMINAL SAFE STATE        = PASS
CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.
```
