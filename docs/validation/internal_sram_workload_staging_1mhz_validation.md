# Internal-SRAM Native Workload Staging Validation at 1.000 MHz

- Date: 2026-08-27
- Hardware: Waveshare RP2350-PiZero and original Pi86 V20/V30 HAT
- Physical processor: Intel `P8086-2`
- Processor identity: native `AAD 16` witness, automatically identified
- Configured processor clock: 1.000 MHz
- Firmware feature commit: `e1ada75`
- Host auto-identity commit: `f13434b`
- Result: **PASS — Host workload staged in RP2350 Internal SRAM**

## Purpose

This validation establishes the first physical Host-to-Internal-SRAM workload
path in the canonical runtime. A flat native binary was transferred from the
Windows Host through the 64-byte HID ABI, checked by the firmware workload
manager, written to the configured Internal-SRAM backing, and published as a
ready workload while the physical Intel 8086 heartbeat remained active.

## Artifacts

Firmware:

```text
rp86_internal_sram_staging-e1ada75.uf2
size   = 143872 bytes
SHA256 = 9A050CF1E04DE4D8BCFA3E6C86823DF86BE959FD9F59252BC0F375C7ACBF1DC7
```

Native workload:

```text
hello-aad16-e1ada75.bin
size   = 157 bytes
SHA256 = E4EB866051DDCA8EF0CD8458E4E96761FBBE8909BD1515E510961BEB1A0AFA8D
CRC32  = A0DFCCDA
```

Declared placement:

```text
processor address = 0x10000
entry point       = 1000:0000
backing resource  = RP2350 Internal SRAM
```

## Host procedure

```powershell
py tools\rp86.py --interactive --heartbeat --attach
```

At the RP86 prompt:

```text
load C:/Users/CCTSAO/Downloads/hello-aad16-e1ada75.bin --address 0x10000 --entry 1000:0000
status
top
status
```

## Retained output

```text
Auto-selected CDC port = COM27

[8086-CLASS PROCESSOR INTERACTIVE HEARTBEAT]
Host runtime shell: type help for the complete command framework.
Heartbeat runs in the background; command traffic has priority.

[PROCESSOR IDENTITY] INTEL 8086 (native AAD 16) automatically identified
Native workload upload
  image   157 bytes
  address 0x10000
  entry   1000:0000
  CRC32   A0DFCCDA
workload upload: PASS (6 records)
  workload_id=1 state=READY detail=157
INTEL 8086 ALIVE=True completed=25 lost=0 min/avg/max=1.7/2.6/3.3 ms
boot_id=1 cpu_seq=35 command_seq=0
workload status: PASS (1 records)
  workload_id=1 state=READY detail=157
INTEL 8086 ALIVE=True completed=25 lost=0 min/avg/max=1.7/2.6/3.3 ms
boot_id=1 cpu_seq=35 command_seq=0
Physical processor runtime top
  INTEL 8086 ALIVE @ 1.000 MHz
  Heartbeat 25 completed / 0 lost
  Latency   2.6 ms average
  Workload  staging id=1 state=2 detail=157
  PSRAM     NOT AVAILABLE
  flash:    RP-FLASH FAT16 AVAILABLE
  sd:       NOT AVAILABLE
workload status: PASS (1 records)
  workload_id=1 state=READY detail=157
| ● INTEL 8086 ALIVE  cpu_seq=000039  rtt=2.9 ms  lost=0
8086>
```

`state=2` in the current `top` presentation is the wire-level value for
`READY`; the explicit workload-status decoder prints the same state by name.

## Accepted invariants

- The Host automatically identified the physical Intel 8086 from its native
  `AAD 16` behavior.
- All six workload-transfer records completed successfully.
- Firmware accepted the declared `0x10000` load address and `1000:0000` entry.
- The transferred image length and CRC32 matched the Host manifest.
- Firmware assigned workload ID 1 and published state `READY` with detail 157.
- A subsequent status request returned the same workload identity, state, and
  size.
- The physical Intel 8086 remained responsive throughout the upload with 25
  completed heartbeats and zero loss.
- The test required no external PSRAM; Internal SRAM was the active workload
  backing resource.

## Scope boundary

This result proves workload transport, validation, placement, retention, and
publication in Internal SRAM. It does **not** yet prove that the Intel 8086
fetched or executed the staged image.

The following remain open:

- arbitrary-address Internal-SRAM response through the canonical PIO/DMA bus
  engine;
- reset handoff to the declared entry point;
- `run`, `stop`, and `restart` lifecycle integration;
- execution output from this staged `hello` workload.

The physical processor continued running the established interrupt-driven
heartbeat workload while the new image was staged. The next integration step
is therefore execution, not another transport or memory-capacity experiment.
