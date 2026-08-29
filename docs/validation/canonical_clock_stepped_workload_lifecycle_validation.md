# Canonical CLOCK_STEPPED Workload Lifecycle Validation

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
load build/firmware/generated/runtime_lifecycle/runtime_lifecycle.bin \
  --clock clock-stepped
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
