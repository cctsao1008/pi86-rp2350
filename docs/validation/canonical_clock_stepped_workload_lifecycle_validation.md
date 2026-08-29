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
