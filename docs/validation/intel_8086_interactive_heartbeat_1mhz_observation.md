# Intel P8086-2 Interactive Heartbeat Observation at 1.000 MHz

- Date: 2026-08-25
- Hardware: Waveshare RP2350-PiZero and original Pi86 V20/V30 HAT
- Physical processor: Intel `P8086-2`
- Configured processor clock: 1.000 MHz
- Runtime target: `companion_runtime_1mhz`
- Host mode: interactive heartbeat with `--attach`
- Result: **PASS — interactive observation only**

## Observed conclusion

With power removed, the NEC V30 was replaced by an Intel `P8086-2`. After the
assembly was powered again, the existing persistent companion runtime completed
55 sequence-numbered heartbeat exchanges with zero loss. The same session then
completed one interactive command exchange.

This proves that the installed Intel 8086 executed enough of the existing native
runtime to remain interruptible, enter the mailbox service path, and produce
request/reply progress visible to the Host. It is strong experimental evidence
that the physical runtime is not inherently limited to the NEC V30.

## Scope and limitation

This run used `--attach`; it did not retain the complete cold-start RESET,
`FFFF0h`, ROM-fetch, first-response, and startup INTR/INTA acceptance transcript.
That limits the retained transcript, but it does not block Intel 8086 support or
the expanded canonical processor scope.

The Host version used for this observation printed fixed `V30` labels. Those
labels were presentation strings and protocol-version-1 payload text, not CPU
auto-detection. The physical processor had been manually replaced with the Intel
`P8086-2`. A later Host update adds explicit `--processor intel-8086` metadata
while retaining the deployed wire payload for compatibility.

Heartbeat latency is end-to-end Host/USB/runtime latency and must not be treated
as an Intel 8086 versus NEC V30 performance comparison.

## Host command

```powershell
py tools\ai_bridge\v30bridge.py --interactive --heartbeat --attach `
  --port COM27 --display status --interval 1.0 `
  --output-dir D:\pi86-validation-logs
```

## Retained output

```text
[V30 INTERACTIVE HEARTBEAT]
Commands: ping, status, send <text>, quiet, verbose, help, quit
Heartbeat runs in the background; command traffic has priority.

[004] V30 HEARTBEAT OK  latency=2.2 ms
V30 ALIVE=True completed=6 lost=0 min/avg/max=1.9/2.6/3.1 ms
Commands: ping, status, send <text>, quiet, verbose, help, quit
Heartbeat display: verbose
[021] V30 HEARTBEAT OK  latency=2.9 ms
[022] V30 HEARTBEAT OK  latency=1.7 ms
[023] V30 HEARTBEAT OK  latency=2.7 ms
[024] V30 HEARTBEAT OK  latency=2.8 ms
[025] V30 HEARTBEAT OK  latency=2.5 ms
[026] V30 HEARTBEAT OK  latency=2.2 ms
[027] V30 HEARTBEAT OK  latency=3.0 ms
[028] V30 HEARTBEAT OK  latency=3.0 ms
[029] V30 HEARTBEAT OK  latency=3.0 ms
[030] V30 HEARTBEAT OK  latency=2.6 ms
[031] V30 HEARTBEAT OK  latency=2.9 ms
[032] V30 HEARTBEAT OK  latency=2.7 ms
[033] V30 HEARTBEAT OK  latency=2.3 ms
Unknown command: 'quilt'; type help
[034] V30 HEARTBEAT OK  latency=2.6 ms
[035] V30 HEARTBEAT OK  latency=2.9 ms
[036] V30 HEARTBEAT OK  latency=2.8 ms
[037] V30 HEARTBEAT OK  latency=2.4 ms
[038] V30 HEARTBEAT OK  latency=2.8 ms
[039] V30 HEARTBEAT OK  latency=3.7 ms
[040] V30 HEARTBEAT OK  latency=3.8 ms
Heartbeat display: quiet (errors and commands only)
[052] V30 HEARTBEAT OK  latency=2.5 ms
V30 ALIVE=True completed=55 lost=0 min/avg/max=1.7/2.7/3.8 ms
[064] V30 COMMAND OK  latency=2.3 ms
V30>
```

## Next formal step

For stronger evidence, run the Intel `P8086-2` from a cold power cycle with explicit processor metadata
and retain the complete startup evidence: RESET qualification, first `FFFF0h`
fetch, first response, native ROM execution, software interrupt service,
physical INTR/two-cycle INTA, heartbeat, command exchange, and terminal bus
safety.

> **The V30 was not an accident. A real Intel 8086 entered the same
> runtime—and answered.**
