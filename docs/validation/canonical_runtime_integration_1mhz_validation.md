# Canonical CDC+HID Runtime Integration at 1.000 MHz

- Date: 2026-08-26
- Hardware: Waveshare RP2350-PiZero and original Pi86 V20/V30 HAT
- Physical processor: Intel `P8086-2`, declared by the Host
- Canonical target: `rp86_rp2350`
- Processor clock: 1.000 MHz
- Result: **PASS**

## What changed

The accepted RP86 processor runtime is no longer a separate characterization-only
UF2. The canonical `rp86_rp2350` target now contains one composite USB device
with:

- CDC status and Host-directed UF2 control;
- fixed 64-byte HID records;
- the accepted PIO/DMA physical-bus engine;
- native software `INT 60h` service;
- physical INTR and two-cycle INTA;
- persistent heartbeat and command mailbox; and
- retained passive bus observation.

General workload upload, arbitrary PSRAM-backed execution, FAT volumes, and
DVI/PIO-USB services remain explicitly unimplemented.

## Build identity

```text
Target     = rp86_rp2350
UF2 size   = 85 KB
UF2 SHA256 = fb1e89ce5fdd7cdc323f8ff63fa71896a8233740c7dda72c8d1913d9e96a50f2
ROM size   = 401 bytes
ROM SHA256 = d2afcfe8e75c3b2688fda37a2a674824c7b22b15b30a65a809cef96f17a315ee
```

## Physical sequence

1. The previous status firmware accepted `PI86 BOOTLOADER` on CDC.
2. Windows identified `E:` as a healthy removable FAT volume labelled
   `RP2350` before the canonical UF2 was copied.
3. The new composite device enumerated as CDC `COM27` and HID
   `CAFE:4011`, serial `A1D538EA0A07378F`.
4. A CDC status request returned `IDLE`, `1.000 MHz`, and
   `WAITING FOR HID RECORD` without releasing processor RESET.
5. The Host sent the initial HID record with `--processor intel-8086`. The
   physical Intel 8086 entered the native RP86 processor runtime and the interactive
   heartbeat became alive.
6. The final image's startup session completed 17 sequence-numbered exchanges
   with zero loss.
7. A later attach session completed 39 more exchanges with zero loss;
   `send HELLO` returned `8086 COMMAND OK`.
8. A concurrent CDC status request returned `RUNNING` and
   `RP86 PROCESSOR RUNTIME` while the processor remained active.
9. The active runtime accepted a CDC bootloader request, stopped the physical
   interface safely, and entered the RP2350 ROM bootloader. The same canonical
   UF2 was then restored.

## Retained Host summary

```text
[INTEL 8086 INTERACTIVE HEARTBEAT]
Host runtime shell: type help for the complete command framework.
Heartbeat runs in the background; command traffic has priority.

| ● INTEL 8086 ALIVE  seq=010  last=2.4 ms  lost=0
8086>

INTEL 8086 heartbeat stopped: completed=17 lost=0 avg=8.3 ms
[019] 8086 COMMAND OK  latency=2.6 ms
INTEL 8086 heartbeat stopped: completed=39 lost=0 avg=2.5 ms
```

The first HID transaction includes startup and therefore dominates the maximum
latency. Later observed heartbeat and command transactions were approximately
1.7-3.7 ms. These are Host-visible transaction times, not processor instruction
benchmarks.

## Evidence

- [Final-image startup CDC evidence](evidence/canonical_runtime_8086_20260826_011858+0800.log)
- [Final-image startup session](evidence/canonical_runtime_8086_20260826_011858+0800.json)
- [Final-image attach/command CDC evidence](evidence/canonical_runtime_8086_20260826_011952+0800.log)
- [Final-image attach/command session](evidence/canonical_runtime_8086_20260826_011952+0800.json)

This validation proves the integrated canonical control, transport, and
physical companion path on the installed Intel 8086. It does not claim that
the unimplemented general PSRAM/FAT/workload features are complete.

## 2026-09-03: Host-independent boot identity

This update supersedes the historical first-HID startup behavior above.
Firmware now executes the prepared AAD16 diagnostic at boot, without waiting
for a CDC reader or consuming the first Host request. The internal bootstrap
record produces no unsolicited HID reply. The resulting physical identity is
retained in structured status, including after general workload execution.

Verified on Intel 8086, device `A1D538EA0A07378F` / COM27:

- CDC status first reported `INTEL 8086 (AAD16=0012) IDENTIFIED`.
- Status-first `INVSQRT.P86W` regression passed without an intervening reboot.
- After one deliberate reboot for the Web-first case, a Web runtime owner
  obtained Intel identity from structured status without sending a native probe.
- Two subsequent CLI regressions shared that broker and both passed without
  reboot: `NATIVE_HLT`, 3,212 cycles, signature `0012`, exact `RESULT: PASS`.
- Post-workload broker status passed. A strict `nec-v30` assertion was rejected
  before upload on the installed Intel CPU.
- Host tests: 135 passed; runtime tests: 31 passed; firmware build passed.

Local raw CDC/session JSON evidence remains under
`C:/Users/CCTSAO/Documents/pi86-validation-logs/`, using the paired filenames
`runtime_session_20260903_002944+0800` (status-first),
`runtime_session_20260903_003114+0800` (Web-first regression), and
`runtime_session_20260903_003117+0800` (repeat regression).
These local files are not repository-hosted evidence links. Retained identity
proves the boot diagnostic result, not current workload liveness.
