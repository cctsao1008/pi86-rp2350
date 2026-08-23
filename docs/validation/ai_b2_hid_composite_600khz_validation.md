# AI-B2-HID Composite Bridge 0.600 MHz Validation

- Date: 2026-08-23
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.600 MHz
- Target: `ai_bridge_hid_mailbox_600khz`
- Firmware source commit: `54ace89`
- Validation/tool commit: `d4fcf6b`
- UF2 size: 97,280 bytes
- UF2 SHA-256: `463c002ecc276dfc0e9fe510357f397cc5de067ce537e3de2965888c4fef8d0c`
- USB identity: development VID `CAFE`, PID `4011`, serial `A1D538EA0A07378F`
- Native ROM size: 230 bytes
- Native ROM SHA-256: `4fceb34847a713477ce45e4b23a06770d212044f5704154e35b4d94ab1701cb4`
- Deterministic host checks: 38/38
- Response architecture: **fixed / prestaged greeting with input-dependent XOR witness**
- Result: **PASS**

## Accepted conclusion

AI-B2-HID physically validates the provider-neutral 64-byte bridge across a
single CDC+HID composite USB device. Windows sent sequence 1 and payload
`HELLO NEC V30` as one complete HID OUT record. The physical NEC V30 fetched
`FFFF0`, received `00EA`, executed the internal-SRAM-backed ROM at `F0000`,
read all seven mailbox words, and returned `HELLO OPENAI CODEX` as one complete
HID IN record with the same sequence.

CDC carried no application request or reply. It remained a receive-only
physical-evidence channel and independently proved reset qualification,
atomic publication after the V30 observed NOT_READY, the READY transition,
native mailbox reads, XOR witness, reply/commit I/O, qualified PIO pairs, zero
response-deadline misses, and terminal electrical safety.

The Windows bridge cross-validated both transports and saved a structured JSON
result. All 38 deterministic checks passed with zero errors. The raw CDC log
ended with RESET high, CLK low, and AD high-Z. This accepts AI-B2-HID. It does
not yet claim that Codex itself initiated the physical exchange; that remains
the AI-B3 end-to-end gate.

The V30 reply text was prestaged as literal native ROM words and was not
derived from the request payload. The input-dependent result in this gate was
the XOR witness over the seven words physically read at `00E4h`. This
classification preserves the distinction between a proven bidirectional
transport and a future content-derived challenge/response computation.

## Exact retained artifacts

- [Raw CDC evidence](evidence/ai_b2_hid_20260823_050201+0800.log)
  - SHA-256: `71ed5b1f55656e2b27f9d30542cf84ef0812398e5ecf01c3246880177a1e84f4`
- [Machine-readable exchange result](evidence/ai_b2_hid_20260823_050201+0800.json)
  - SHA-256: `d74ab3a6a4231a89797073be86b4621061a4586e842c86f427811944fa89acfb`

The JSON `cdc_validation.raw_sha256` equals the retained raw log SHA-256. Both
artifacts are marked `-text` in `.gitattributes` so Git preserves their exact
Windows evidence bytes.

## Key physical evidence

| Evidence | Physical result |
|---|---:|
| HID OUT request | 64 bytes, sequence 1, `HELLO NEC V30` |
| HID IN reply | 64 bytes, sequence 1, `HELLO OPENAI CODEX` |
| Reset vector / first response | `FFFF0` / `00EA` PASS |
| ROM execution | `F0000` PASS |
| STATUS transition | `0000h` to `0001h` PASS |
| Publication ordering / atomicity | PASS / PASS |
| Mailbox RX `00E4h` | 7/7 words PASS |
| V30 XOR `00E8h` | PASS |
| Mailbox TX / commit | `00E2h` / `00E6h` PASS |
| ROM / mailbox qualified pairs | 121/121 / 9/9 PASS |
| Response deadline misses | 0 PASS |
| Current-cycle M33 | NONE |
| CDC role | RECEIVE-ONLY PASS |
| Python acceptance checks | 38/38 PASS |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |

## Canonical exchange

```text
OpenAI Codex > HELLO NEC V30  (HID, 64 bytes)
NEC V30      > HELLO OPENAI CODEX  (HID, 64 bytes)
```

The host-side label describes the accepted protocol endpoint. For this AI-B2
run, the user launched the Python bridge; AI-B3 will require Codex to launch
the same bridge and consume its JSON result directly.
