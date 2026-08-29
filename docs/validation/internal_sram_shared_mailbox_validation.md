# Internal SRAM Shared-Mailbox Physical Validation

## Result

**PASS — 2026-08-30, physical Intel 8086 at 1.000 MHz.**

This validation closes a complete shared-memory path:

```text
Host text
  -> 64-byte HID memory records
  -> RP2350 Internal SRAM at 3F000h
  -> physical Intel 8086 polling workload
  -> uppercase conversion by native instructions
  -> ownership returned to Host
  -> Host reads the result
```

It is not a Host-side calculation and does not use an emulator.

## Candidate

- firmware: `build/firmware/rp86_rp2350.uf2`
- workload: `build/firmware/generated/shared_mailbox/shared_mailbox.bin`
- workload size: 99 bytes
- load address / entry: `10000h` / `1000:0000`
- mailbox: `3F000h-3FFFFh`
- execution policy: `CLOCK_STEPPED`

## Physical transcript

```text
Native workload upload
  image   99 bytes
  address 0x10000
  entry   1000:0000
  clock   CLOCK-STEPPED
  CRC32   4543F323
workload upload: PASS (5 records)

workload run: PASS (1 records)
  workload_id=1 state=RUNNING detail=99 clock=CLOCK-STEPPED

8086> mailbox Hello from Host to real 8086
mailbox: PASS generation=1 processor=HELLO FROM HOST TO REAL 8086
```

The retained memory view independently exposes the ABI state and result:

```text
3F000  52 38 36 4D 01 00 20 00 01 00 03 00 01 00 00 00  |R86M.. .........|
3F010  1C 00 1C 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
3F020  48 45 4C 4C 4F 20 46 52 4F 4D 20 48 4F 53 54 20  |HELLO FROM HOST |
3F030  54 4F 20 52 45 41 4C 20 38 30 38 36 00 00 00 00  |TO REAL 8086....|
```

Decoded header state:

- magic: `R86M`
- owner: `HOST`
- status: `RESULT_READY`
- generation: `1`
- request length: `28`
- response length: `28`

General Host memory access was also checked while the workload was active:

```text
8086> mem write 0x20000 DE AD BE EF
mem write: PASS  4 bytes at 0x20000
8086> mem read 0x20000 4
20000  DE AD BE EF                                      |....|
```

The processor was finally stopped through the normal lifecycle and entered
`STOPPED / RESET` after 837445 serviced cycles.

## Evidence

- `D:\pi86-validation-logs\companion_heartbeat_20260830_045246+0800.log`
- `D:\pi86-validation-logs\companion_heartbeat_20260830_045246+0800.json`
- outer prepared-runtime heartbeat: 74 completed, 0 lost

The exact final UF2, rebuilt after capability reporting was updated, was flashed
again and passed a shorter independent smoke test:

```text
mailbox: PASS generation=1 processor=FINAL CANDIDATE PASSED
workload stop: PASS
  workload_id=1 state=STOPPED detail=99 clock=STOPPED
  cycles=379916 processor=STOPPED / RESET
```

- `D:\pi86-validation-logs\companion_heartbeat_20260830_050051+0800.log`
- `D:\pi86-validation-logs\companion_heartbeat_20260830_050051+0800.json`
- final-candidate outer heartbeat: 33 completed, 0 lost

## Boundary of this claim

This proves Host read/write access, the fixed Internal-SRAM mailbox ABI,
ownership transfer, physical Intel 8086 polling and transformation, and result
retrieval. It does not claim interrupt-driven mailbox wakeup, general
stdin/stdout queues, External PSRAM backing, or NEC V30 regression coverage.
