# Host-Loaded Internal-SRAM Calculator — 1 MHz Physical Intel 8086 Validation

## Result

**PASS** — on 2026-08-28, the Host uploaded a 16-byte native calculator image
into the RP2350 Internal-SRAM workload pool, started it, exercised all four
operations on a physical Intel `P8086-2`, stopped it, restarted it, and
confirmed continued native execution with zero heartbeat loss.

This extends the earlier pre-staged calculator service with a bounded
Host-loaded execution path. It does not claim general arbitrary-image execution
or general writable processor RAM.

## Physical configuration

- Processor: Intel `P8086-2`, automatically identified by native `AAD 16`
- Processor clock: 1.000 MHz companion runtime
- Host transport: 64-byte USB HID records
- Evidence transport: receive-only USB CDC
- Workload backing: RP2350 Internal SRAM
- Load address: physical `10000h`
- Entry point: `1000:0000`
- Interrupt path: physical `INTR` and two-cycle `INTA`
- Current-cycle M33 response: none

Firmware artifact:

```text
rp86_internal_sram_calculator_1mhz.uf2
SHA-256 53383f5c720cc44343cf263cdb4c31787da130f4278037b571764fbf31947160
```

Host-loaded workload artifact:

```text
calculator_workload.bin
size   16 bytes
CRC32  C967752D
SHA-256 6afb6e036ccc330c0efb0646da480a0682b8d07411d9f6245fafbab9169c44d0
```

## Upload and lifecycle evidence

```text
Native workload upload
  image   16 bytes
  address 0x10000
  entry   1000:0000
  CRC32   C967752D
workload upload: PASS (3 records)
  workload_id=1 state=READY detail=16
```

The same workload then completed the lifecycle transitions:

```text
READY -> RUNNING -> STOPPED -> RUNNING
```

The Host received `PASS` replies for `run`, `stop`, and `restart`, and workload
status reported the corresponding state after each transition. Heartbeat and
control remained available while the workload was stopped.

## Native calculator exchanges

With the uploaded workload running, the physical Intel 8086 returned:

```text
[047] CALC 12+34=46        latency=2.1 ms
[048] CALC 1000-7=993      latency=2.8 ms
[049] CALC 300*200=60000   latency=3.7 ms
[050] CALC 1000/33=30 R10  latency=5.4 ms
```

After `restart`, a further division request returned:

```text
CALC 1000/33=30 R10  latency=3.9 ms
```

The retained clean session ended with:

```text
INTEL 8086 heartbeat stopped: completed=73 lost=0 avg=2.7 ms
```

The session JSON recorded a latency range of 1.7823–5.4282 ms, average 2.6646
ms, `boot_id=1`, and final native `cpu_sequence=107`.

## Physical execution evidence

For each calculator request, CDC retained:

```text
[LIVE CPU ROUND] INTA=PASS commit=PASS EOI=PASS idle=PASS result=PASS
[NATIVE PROCESSOR ID] signature=0012 identity=INTEL 8086 result=PASS
```

It also retained the native operation witnesses:

```text
[NATIVE CALCULATOR] op=1 lhs=12   rhs=34  low=46    high=0  result=PASS
[NATIVE CALCULATOR] op=2 lhs=1000 rhs=7   low=993   high=0  result=PASS
[NATIVE CALCULATOR] op=3 lhs=300  rhs=200 low=60000 high=0  result=PASS
[NATIVE CALCULATOR] op=4 lhs=1000 rhs=33  low=30    high=10 result=PASS
```

The canonical PIO sequence dispatches a far call to the selected entry inside
the uploaded Internal-SRAM image. The physical processor fetches and executes
that native instruction, returns through the prepared far-return path, commits
the result, issues EOI, and returns to the persistent idle loop.

Raw Host evidence:

```text
D:\\pi86-validation-logs\\companion_heartbeat_20260828_043231+0800.log
D:\\pi86-validation-logs\\companion_heartbeat_20260828_043231+0800.json
```

## Scope boundary

This gate proves one deliberately small but complete path:

```text
Host load -> Internal SRAM -> lifecycle control -> physical fetch/execute
          -> interrupt mailbox result -> Host observation
```

The workload is a fixed 16-byte, four-entry calculator image. General binary
layouts, arbitrary control flow, writable processor RAM, and PSRAM-backed
execution remain later integration work.
