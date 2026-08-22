# AI-B3 Codex-Initiated Physical Greeting Validation

- Date: 2026-08-23
- Initiator: OpenAI Codex desktop task, direct local command execution
- Host: Windows Python bridge in `D:\my-github\pi86-rp2350`
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 0.600 MHz
- Firmware target: `ai_bridge_hid_mailbox_600khz`
- Firmware source commit: `54ace89`
- Host/document baseline before evidence: `8032def`
- USB identity: development VID `CAFE`, PID `4011`, serial `A1D538EA0A07378F`
- Deterministic host checks: 38/38
- Result: **PASS**

## Accepted conclusion

AI-B3 validates the first Codex-initiated end-to-end exchange with the physical
NEC V30. After the user reset the already accepted AI-B2-HID firmware, Codex
directly launched the Windows bridge command from its desktop task. The user
did not type or launch the successful exchange command.

Codex sent sequence 1 and `HELLO NEC V30` through the provider-neutral Python
bridge as one 64-byte HID OUT record. The physical V30 fetched its reset vector,
executed native code, consumed all seven mailbox words, and returned sequence 1
and `HELLO OPENAI CODEX` as one 64-byte HID IN record. Codex received and parsed
the resulting JSON directly.

The simultaneous receive-only CDC evidence passed all 38 AI-B2-HID checks with
zero errors and zero response-deadline misses. The run ended RESET-high,
CLK-low, and AD-high-Z. This accepts the Codex adapter as the first AI host for
the provider-neutral bridge. It does not restrict later ChatGPT app/MCP,
OpenAI API, or other host adapters.

## Invocation provenance

Codex executed the following command through its local command tool with user
approval:

```powershell
py tools\ai_bridge\v30bridge.py --exchange --port COM27 --json `
  --output-dir D:\pi86-validation-logs\codex-initiated
```

The first attempt overlapped Windows USB re-enumeration after RESET and
produced an empty CDC capture plus no HID reply. It was correctly rejected and
is not retained as acceptance evidence. Codex retried only after enumeration
was stable; the second attempt below is the accepted run.

## Exact retained artifacts

- [Raw CDC evidence](evidence/ai_b3_codex_20260823_051243+0800.log)
  - SHA-256: `71ed5b1f55656e2b27f9d30542cf84ef0812398e5ecf01c3246880177a1e84f4`
- [Machine-readable Codex exchange result](evidence/ai_b3_codex_20260823_051243+0800.json)
  - SHA-256: `561e0125bba2a9bc5d2acb350243fc053026671368ef9b3a6bb7dd4e56886dbf`

The raw CDC SHA matches the earlier user-launched AI-B2 run because the
physical firmware transcript is intentionally deterministic. The distinct
JSON records the accepted Codex-run timestamp and `codex-initiated` artifact
path.

## Accepted exchange

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

| Evidence | Result |
|---|---:|
| Initiator | Codex local command tool |
| HID request/reply size | 64 / 64 bytes PASS |
| Request/reply sequence | 1 / 1 PASS |
| V30 reply | `HELLO OPENAI CODEX` PASS |
| CDC role | RECEIVE-ONLY PASS |
| Deterministic checks | 38/38 PASS |
| Response deadline misses | 0 PASS |
| Bus ownership/safety | PASS |
| Terminal RESET-high, CLK-low, AD-high-Z | PASS |
