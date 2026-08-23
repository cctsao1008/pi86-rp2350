# Persistent Companion Runtime 1.000 MHz Validation

- Date: 2026-08-23
- Hardware: Waveshare RP2350-PiZero with physical NEC V30 Pi86 HAT
- Configured V30 clock: 1.000 MHz
- Target: `companion_runtime_1mhz`
- Firmware source commit: `208e57e`
- Validation/tool commit: `a6b4076`
- UF2: `companion_runtime_1mhz_interactive.uf2`
- UF2 size: 80,896 bytes
- UF2 SHA-256: `82714cb5efbf8df08b25a5f9a7735fb7e869c24c40749d017c81fdb55a7666bd`
- Response architecture: **bounded exact-stream ROM/I/O service plus interrupt-driven persistent mailbox**
- Result: **PASS**

## Accepted conclusion

This gate accepts the first persistent physical-V30 runtime in the project.
The run does not terminate by asserting RESET. After native boot and software
`INT 60h` service, the V30 enables interrupts and remains alive in an
interruptible `STI`/`HLT` idle loop with RESET released, its clock running,
and the multiplexed AD bus released while idle.

The startup acceptance proved eight physical INTR assertions, eight first
INTA accepts, eight second INTA completions, native mailbox commit, native EOI,
PIO1 non-AD isolation, and no current-cycle M33 response. The PIO1 state
machines independently own foreground ROM/I/O, IRQ ROM, IRQ I/O, and INTA
service within the complete 32-instruction budget.

The host then completed 118 fresh sequence-and-nonce heartbeat exchanges with
zero loss. Every retained CDC round proves INTA, the request-dependent V30
witness, reply commit, EOI, return to idle, and an overall PASS. After the host
monitor exited without resetting the V30, a new process used `--attach` and
completed another 82 exchanges with zero loss. This separately accepts host
reattachment to the same powered persistent runtime.

This is a bounded companion-runtime acceptance, not a claim of a complete PC
PIC/PIT subsystem or DOS runtime. Heartbeat proves interrupt-driven V30
liveness and the defined mailbox service; it does not by itself prove arbitrary
application progress.

## Key physical evidence

| Evidence | Physical result |
|---|---:|
| Configured V30 clock | 1.000 MHz |
| RESET clock qualification | PASS |
| Software `INT 60h` commit | PASS |
| Physical INTR assertions | 8 |
| INTA first / second cycles | 8/8 / 8/8 PASS |
| IRQ mailbox commit / native EOI | PASS / PASS |
| PIO1 non-AD isolation | PASS |
| Current-cycle M33 | NONE |
| Persistent V30 state | `STI`/`HLT`, IRQ heartbeat armed |
| Startup deterministic checks | 19/19 PASS |
| Clean heartbeat session | 118 complete, 0 lost |
| Clean-session latency | 1.649 / 3.406 / 103.437 ms min/avg/max |
| Reattach heartbeat session | 82 complete, 0 lost |
| Reattach latency | 1.612 / 2.288 / 3.314 ms min/avg/max |

The 103.437 ms first-round maximum in the 118-round session is a host-attach
startup outlier. The subsequent independent reattach session bounds all 82
measured HID request/reply rounds between 1.612 and 3.314 ms.

## Exact retained artifacts

### Startup and persistent-runtime acceptance

- [Raw CDC evidence](evidence/companion_runtime_20260823_205245+0800.log)
  - SHA-256: `5f2b369815ace962b872b232f7dce6a1ec2d1fafe7aef081e91bafad956f0828`
- [Machine-readable acceptance result](evidence/companion_runtime_20260823_205245+0800.json)
  - SHA-256: `39bb109ada806406febcdc84adb20e007517acb12f5e85c1441f2e810cc0e9a9`

### 118-round clean heartbeat session

- [Raw CDC heartbeat evidence](evidence/companion_heartbeat_20260823_211828+0800.log)
  - SHA-256: `d7d39db359230d9dd9424d11d2451255ad90e5bb158d02b5bd12188b7bd0b523`
- [Machine-readable heartbeat result](evidence/companion_heartbeat_20260823_211828+0800.json)
  - SHA-256: `98a0755238b6c3e98498b5f30ea3ee226e834e6dfa75af83a5864df9666fc97f`

### 82-round same-runtime reattach session

- [Raw CDC heartbeat evidence](evidence/companion_heartbeat_20260823_212131+0800.log)
  - SHA-256: `25c4ae23bb082e9340a0fee8ef800ce5a3dfaa9882c457fd4e7e7da277adcd21`
- [Machine-readable heartbeat result](evidence/companion_heartbeat_20260823_212131+0800.json)
  - SHA-256: `404b964fe6c40ecc957daeace7b9d5375179beb6b0778ad1d8039ed58ef89764`

All six files are retained without newline normalization. The heartbeat JSON
records contain the HID identity, every sequence, request type, measured
latency, pass/fail result, aggregate loss count, and path to the corresponding
raw CDC evidence.

## Lifecycle distinction

The accepted bounded-validation terminal state remains RESET high, CLK low,
and AD high-Z. This persistent runtime intentionally uses a different accepted
state:

```text
RESET released
CLK running at 1.000 MHz
V30 in STI/HLT while idle
AD bus released while idle
physical INTR/INTA heartbeat armed
```

Stopping or detaching the Windows monitor does not assert V30 RESET. A new
host process may use `--attach` only while that same accepted powered runtime
continues to execute. After a USB power cycle or RP2350 reset, the full startup
acceptance must run again.
