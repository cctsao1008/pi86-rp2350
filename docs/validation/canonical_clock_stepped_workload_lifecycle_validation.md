# Canonical CLOCK_STEPPED Workload Lifecycle Validation

## Execution deadline — 2026-09-03

The candidate UF2 was flashed once through HID bootloader control, then tested
on the physical Intel 8086 using:

```powershell
py tests\physical\lifecycle_recovery.py --persistent build\workloads\LIFECYCLE.P86W --recovery build\workloads\INVSQRT.P86W --diagnostics --execution-deadline --output-dir build\validation\execution-deadline
```

All 13 lifecycle cases passed, including the original stop/restart/fault
recovery cases. New deadline results:

- A busy loop with a 400 ms limit reached `TIMED_OUT / EXECUTION_DEADLINE`,
  stopped/reset after 5,126 serviced cycles, and retained its diagnostic snapshot.
  The Host made no requests during a 700 ms sleep after run acceptance. USB
  remained connected; this was not a physical cable-disconnect test.
- After timeout, INVSQRT loaded and completed with `RESULT: PASS` at 3,748
  cycles. With a five-second limit it also restarted and completed normally;
  completion disarmed the timer.
- With the limit OFF, the loop remained RUNNING after the same 700 ms wait.
  Explicit stop succeeded at 11,248 cycles; INVSQRT then completed again.
- The shell's actual `timeout`, `timeout 5`, query, and `timeout off` HID paths
  returned the expected settings. Only keyboard input was supplied by a test
  driver; the shell, broker, USB and firmware were real.

Local machine-readable evidence (not committed build artifacts):

```text
build/validation/execution-deadline/runtime_session_20260903_123544+0800.json
build/validation/execution-deadline/shell/runtime_session_20260903_123917+0800.json
```

The final device setting was restored to OFF, with INVSQRT completed. Host
tests cover framing, validation and shell-value parsing; executor unit tests
cover exact boundaries, unchanged deadlines on query, original-start semantics
on SET, restart, completion, OFF and unsupported prepared execution. No PIO,
ISR or bus-cycle timing implementation was changed.

## Stopped fault diagnostics — 2026-09-03

Updated firmware was flashed once to the same Intel 8086 system. The opt-in
physical lifecycle test with `--diagnostics` verified:

- reading while executing returns BAD_STATE without stopping execution;
- accepted upload clears the preceding diagnostics;
- fault reads are repeatable and do not change cycles;
- the unbacked read fixture reports `BUS_FAULT`, 9 cycles,
  `MEM_READ`, physical address `0x40000`, lanes 3, `UNMAPPED`, and unavailable data;
- new INVSQRT execution after the fault completes with `RESULT: PASS`,
  NATIVE_HLT and 3,212 cycles, without reboot.

The real CLI additionally captured the fault automatically into session JSON.
`trace save` was exercised through the actual interactive command dispatch,
with only keyboard input automated; the saved address/type/reason matched the
physical snapshot. A faulted physical regression now exits immediately with
BUS_FAULT, rather than waiting and mislabeling it as a Host deadline timeout.

Evidence relative to the validation checkout:

```text
build/validation/fault-diagnostics/runtime_session_20260903_113248+0800.json
build/validation/fault-diagnostics/shell/runtime_session_20260903_113540+0800.json
build/validation/fault-diagnostics/automatic-final/runtime_session_20260903_113541+0800.json
build/validation/fault-diagnostics/recovered/runtime_session_20260903_113622+0800.json
```

The CLI directories also retain raw CDC logs. The standalone lifecycle test
retains structured HID snapshots only. Firmware timing code was unchanged;
no-ALE timeout diagnostics remain covered by the deterministic C executor test,
not by a physical starvation claim.

## Recovery regression — 2026-09-03

The current Intel 8086 recovery checks passed after one firmware update, with
no reboot between cases. General execution timing and the 64-byte wire ABI
were unchanged.

A real defect was reproduced first: `run` after `stop` returned RUNNING but
retained the previous `STOP_REQUESTED` reason and native output. Every executor
start now clears the previous result, including early start-failure paths;
`restart` can no longer inherit an old PASS. The shared Python `DeviceClient`
also now reads the broker's canonical `reply_hex` field.

| Physical case | Observed result |
| --- | --- |
| Stop a running LIFECYCLE image | STOPPED / RESET; cycles stable at 4,145 |
| Run the same stopped image | Fresh result at start; stopped again at 950 cycles |
| Complete INVSQRT, then restart it | Both runs: COMPLETED / NATIVE_HLT, `RESULT: PASS`, 3,212 cycles |
| Unannounced `CLI; HLT` | FAULTED / BUS_FAULT at 7 cycles; STOPPED / RESET |
| Load/run INVSQRT after that fault | `RESULT: PASS`, 3,212 cycles |
| Read unbacked physical address 40000h | FAULTED / BUS_FAULT at 9 cycles; STOPPED / RESET |
| Load/run INVSQRT after the memory fault | `RESULT: PASS`, 3,212 cycles |

The no-ALE timeout is **offline tested, not physically induced**. The C test
links the production executor, workload manager, memory and mailbox code,
substituting only bus operations and time. It verifies that completed cycles
reset the starvation interval, 99,999 us does not time out, 100,000 us does,
and a new CRC-verified upload runs after TIMED_OUT. This is a bus-starvation
deadline, not a maximum workload execution duration.

To repeat the physical checks with the built packages (this replaces any
current workload; close other controllers first):

```powershell
py tests\physical\lifecycle_recovery.py --persistent build\workloads\LIFECYCLE.P86W --recovery build\workloads\INVSQRT.P86W --output-dir build\validation\lifecycle-after
```

The script shares the Host broker and never reboots or flashes automatically.
It retains structured HID snapshots, image metadata and terminal results;
it does not capture raw CDC bus evidence. Local reproduction/result JSON:

```text
build/validation/lifecycle-before/runtime_session_20260903_065639+0800.json
build/validation/lifecycle-after/runtime_session_20260903_070330+0800.json
```

The sections below record earlier validation and its then-current semantics.

Date: 2026-08-29  
Physical processor: Intel P8086-2  
Companion: Waveshare RP2350-PiZero on the original Pi86 HAT  
Canonical free-running rate before workload launch: 1.000 MHz

## Result

Canonical `load → run → status → stop → restart` is physically accepted for a
general flat native image in the 256 KiB Internal-SRAM processor tier.

This is not simulation-only evidence. The RP2350 generated a reset handoff at
physical `FFFF0h`, the installed Intel 8086 fetched and executed the uploaded
image at `1000:0000`, native I/O returned text and a numeric result, and Host
control remained responsive while the general bus controller owned execution.

## Host command

```text
load build/workloads/LIFECYCLE.P86W
run
status
stop
restart
status
stop
```

The 46-byte lifecycle image printed `RP86 RUN`, returned `002Ah` after native
`19 + 23`, and remained in a two-byte loop. The first run reached 335,768
serviced physical bus cycles before Host stop. Restart reconstructed the reset
handoff, printed and calculated the same result again, and reached 309,686
cycles before the second stop.

```text
[WORKLOAD START] id=1 entry=1000:0000 clock=CLOCK_STEPPED
[NATIVE STDOUT] RP86 RUN
[NATIVE RESULT] 002A
[WORKLOAD STOP] cycles=335768
[WORKLOAD START] id=1 entry=1000:0000 clock=CLOCK_STEPPED
[NATIVE STDOUT] RP86 RUN
[NATIVE RESULT] 002A
[WORKLOAD STOP] cycles=309686
```

The enclosing prepared-runtime session completed 27 native Intel 8086
heartbeats with zero loss before the general workload took ownership.

## HLT-idle validation

The 164-byte canonical `hello.bin` then exercised the processor-idle contract.
It printed the native AAD-16 identity, armed the runtime idle indication, and
executed HLT. Status remained responsive, the workload remained RUNNING rather
than FAULTED, stop parked CLK low and asserted RESET, and restart repeated the
same native output and HLT transition.

```text
[WORKLOAD START] id=1 entry=1000:0000 clock=CLOCK_STEPPED
[NATIVE STDOUT] HELLO INTEL 8086
[WORKLOAD IDLE] native HLT indication accepted
WORKLOAD ... state=RUNNING ... status=0 REPLIED
[WORKLOAD STOP] cycles=70
[WORKLOAD START] id=1 entry=1000:0000 clock=CLOCK_STEPPED
[NATIVE STDOUT] HELLO INTEL 8086
[WORKLOAD IDLE] native HLT indication accepted
[WORKLOAD STOP] cycles=70
```

Intel documents that minimum-mode software HLT emits one ALE without
qualifying bus-control signals. The fixed HAT does not route `RD`, `WR`, or
`DEN`, so the workload publishes `IDLE_PREPARE` immediately before HLT. The
runtime consumes exactly the following HLT indication as idle; ordinary
unmapped I/O remains a fault. See the Intel 8086 specification's Software Halt
waveform and description:
`https://bitsavers.org/components/intel/8086/9800722-03_The_8086_Family_Users_Manual_Oct79.pdf`.

## Retained evidence

Local raw CDC evidence:

```text
D:\pi86-validation-logs\companion_heartbeat_20260829_155858+0800.log
D:\pi86-validation-logs\companion_heartbeat_20260829_160337+0800.log
```

The corresponding session JSON files retain Host timings, processor identity,
request sequence, loss count, and log paths.

## Accepted boundary

- General Internal-SRAM upload, CRC validation, reset handoff, run, status,
  safe stop, restart, native diagnostic output, result publication, and
  cooperative HLT idle are accepted on the physical Intel 8086.
- CLOCK_STEPPED means complete pulses with safe low intervals; it does not
  claim a fixed 1 MHz wall-clock execution rate.
- Arbitrary FREE_RUNNING images remain unsupported unless their current-cycle
  memory and I/O responses have been prepared for PIO/DMA.
- Shared-memory mailbox, general stdin, interrupt wake from the general HLT
  state, SD, and optional PSRAM capacity remain later integrations.

## Independent-review hardening — 2026-08-30

An independent source review identified supervisory failure paths that the
original successful lifecycle did not exercise. The local fixes preserve the
physical bus architecture and add the following invariants:

- replaying `WORKLOAD_COMMIT` outside `RECEIVING` is rejected without changing
  a healthy workload state;
- a general workload cannot enter FREE_RUNNING after the prepared PIO/DMA
  responder has been retired;
- lack of ALE has a bounded terminal `TIMED_OUT` path instead of remaining
  `RUNNING` indefinitely;
- clock-step failure first makes AD high-Z, asserts RESET, and parks CLK low;
- status reports actual physical clock mode and a separate processor-idle flag;
- `IDLE_PREPARE` is a single-use promise whose first following
  non-serviceable indication is accepted as HLT idle, without depending on the
  floating bus decoding specifically as port zero.

The hardened firmware was physically exercised on the same Intel P8086-2.
The canonical hello workload still printed its native identity and reached
the idle state after 70 serviced cycles:

```text
[WORKLOAD START] id=1 entry=1000:0000 clock=CLOCK_STEPPED
[NATIVE STDOUT] HELLO INTEL 8086
[WORKLOAD IDLE] armed native HLT indication accepted
workload ... state=RUNNING ... clock=CLOCK-STEPPED cycles=70 processor=IDLE
[WORKLOAD STOP] cycles=70
```

An unarmed one-byte HLT workload no longer remained falsely RUNNING. On this
board the unqualified HLT signature appeared as an unmapped I/O-read-like
cycle, so the safe terminal result was `FAULTED` after six serviced cycles:

```text
[WORKLOAD FAULT] cycle=6 address=2E9BA type=IOR unmapped=1 lane=0 pad=0 clock=0 inta=0
[WORKLOAD STOP] cycles=6
workload ... state=FAULTED ... clock=STOPPED
```

A separate eight-byte workload attempted `OUT 00EAh,0001h` from the general
path. The runtime retained the real write cycle and stopped safely rather than
allowing the processor to free-run against an undriven bus:

```text
[WORKLOAD FAULT] cycle=9 address=000EA type=IOW unmapped=1 lane=0 pad=0 clock=0 inta=0
[WORKLOAD STOP] cycles=9
workload ... state=FAULTED ... clock=STOPPED
```

Finally, the 46-byte lifecycle workload again printed `RP86 RUN`, returned
`002A`, stopped after 178,227 cycles, restarted from a new reset handoff,
returned the same output and result, and stopped after 31,865 cycles. The
enclosing prepared-runtime phase completed 26 physical Intel 8086 heartbeats
with zero loss.

Evidence retained on the validation Host:

```text
D:\pi86-validation-logs\companion_heartbeat_20260830_041059+0800.log
D:\pi86-validation-logs\companion_heartbeat_20260830_041059+0800.json
```

The no-ALE starvation timer was not physically induced by removing or
electrically stalling the processor. Its bounded implementation, terminal
state, and Host decoding are build/test accepted here; a future intentional
hardware-starvation test must remain separately identified as physical
evidence.

After the final state-reporting cleanup, the exact candidate UF2 was flashed
again and passed a closing smoke test: upload reported a zero cycle count,
hello reached `IDLE / HLT` at 70 cycles, and stop reported
`STOPPED / RESET`. That final-build evidence is retained at:

```text
D:\pi86-validation-logs\companion_heartbeat_20260830_041959+0800.log
D:\pi86-validation-logs\companion_heartbeat_20260830_041959+0800.json
```
