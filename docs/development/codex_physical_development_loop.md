# Codex-to-Physical-Processor Development Loop

## Purpose

This document records the development loop that now exists around
pi86-rp2350:

> Codex can write native 8086-class code, build the RP2350 runtime, deploy it
> to connected hardware, exercise a real physical processor, interpret the
> returned evidence, update the repository, and retain the accepted result.

This is not simulation-only validation. The decisive result comes from an
installed Intel 8086 or NEC V30 executing native machine instructions on its
physical bus.

The canonical roles remain those defined in [`../architecture.md`](../architecture.md):

```text
Host       = Runtime Controller
RP2350     = Companion Resource and Bus Controller
8086 / V30 = Bare-Metal Remote Physical Processor
```

Codex operates through the Host tools and build environment. It does not
replace any of these architectural roles.

## The closed loop

```text
                 source and intent
                        |
                        v
              Codex writes/reviews code
                        |
                        v
          NASM workload + RP2350 firmware build
                        |
                        v
                  UF2 deployment
                        |
                        v
 Host Python ---- USB HID/CDC ---- RP2350
     |                               |
     |                         prepared PIO/DMA
     |                               |
     |                               v
     +------ result/evidence --- physical 8086/V30
                        |
                        v
             deterministic acceptance
                        |
                        v
          documentation + commit + push
```

The loop has eight distinct stages. Keeping them separate prevents a compiler
success, USB echo, or firmware-generated value from being mistaken for real
processor execution.

## 1. Write the native workload

The workload is authored as 16-bit x86 machine code, normally using NASM. For
the first Host-loaded execution gate, Codex created
[`../../firmware/workloads/calculator.asm`](../../firmware/workloads/calculator.asm).

Its four aligned entry slots contain native operations:

| Entry offset | Operation | Native instruction |
|---:|---|---|
| `0000h` | add | `ADD AX,CX` |
| `0004h` | subtract | `SUB AX,CX` |
| `0008h` | multiply | `MUL CX` |
| `000Ch` | divide | `DIV CX` |

Each entry returns with `RETF`. The complete image is only 16 bytes, which
keeps the first gate bounded and its bus behavior auditable.

## 2. Build both sides

The canonical WSL build assembles the workload and builds the RP2350 firmware:

```bash
cd ~/github/pi86-rp2350
cmake --build build --target pi86_rp2350 -j2
```

The relevant output artifacts are:

```text
build/firmware/pi86_rp2350.uf2
build/firmware/generated/calculator_workload/calculator_workload.bin
```

The accepted 2026-08-28 artifacts were:

| Artifact | SHA-256 |
|---|---|
| `rp86_internal_sram_calculator_1mhz.uf2` | `53383f5c720cc44343cf263cdb4c31787da130f4278037b571764fbf31947160` |
| `calculator_workload.bin` | `6afb6e036ccc330c0efb0646da480a0682b8d07411d9f6245fafbab9169c44d0` |

The workload manifest also carried CRC32 `C967752D`.

## 3. Enter the RP2350 bootloader and deploy UF2

The Host can request UF2 boot mode through the running CDC control path:

```powershell
py tools\rp86.py --bootloader --timeout 5
```

A successful request reports:

```text
RP2350 bootloader request = ACKNOWLEDGED
RP2350 UF2 bootloader     = ENTERING
```

The UF2 is then copied to the RP2350 boot volume. A physical BOOT/RESET action
or USB reconnection remains a valid recovery path when no working control
firmware is present.

## 4. Start the Host runtime

On Windows, the canonical shell is:

```powershell
cd D:\my-github\pi86-rp2350
py tools\rp86.py `
  --interactive --heartbeat --attach `
  --display status --interval 1.0 `
  --output-dir D:\pi86-validation-logs
```

The tool automatically selects the only matching CDC interface. If multiple
devices are present, the operator supplies `--port` so each runtime remains
unambiguous.

The installed processor is identified by native `AAD 16` behavior. Intel 8086
and NEC V30 do not provide CPUID, so the processor itself produces the identity
witness; the Host does not infer it from a label or USB configuration.

## 5. Upload and control the workload

The accepted calculator sequence is:

```text
load C:\Users\CCTSAO\Downloads\calculator_workload.bin --address 0x10000 --entry 1000:0000
status
run
status
send 12+34
calc 1000 - 7
calc 300 * 200
send 1000/33
stop
status
restart
status
calc 1000/33
quit
```

The Host divides the image into 64-byte protocol records. RP2350 validates the
manifest, address bounds, record order, image length, and CRC32 before changing
the workload state to `READY`.

The accepted upload reported:

```text
Native workload upload
  image   16 bytes
  address 0x10000
  entry   1000:0000
  CRC32   C967752D
workload upload: PASS (3 records)
  workload_id=1 state=READY detail=16
```

The lifecycle then completed:

```text
READY -> RUNNING -> STOPPED -> RUNNING
```

## 6. Execute on the physical processor

When the calculator workload is `RUNNING`, the RP2350 prepares a far-call
dispatch to the selected entry in Internal SRAM. PIO/DMA presents the required
instruction and return cycles to the processor bus. M33 software does not
answer the current bus cycle.

For every request, the physical processor:

1. accepts physical `INTR` through a two-cycle `INTA` handshake;
2. enters its native interrupt service routine;
3. receives the operands in registers;
4. far-calls the selected uploaded Internal-SRAM entry;
5. fetches and executes the native arithmetic instruction;
6. returns through `RETF`;
7. commits the result through the processor mailbox;
8. issues EOI and returns through `IRET` to persistent `STI`/`HLT` idle.

The physical Intel 8086 returned:

```text
[047] CALC 12+34=46        latency=2.1 ms
[048] CALC 1000-7=993      latency=2.8 ms
[049] CALC 300*200=60000   latency=3.7 ms
[050] CALC 1000/33=30 R10  latency=5.4 ms
```

After `restart`, it again returned `1000/33=30 R10`.

## 7. Separate results from evidence

The composite USB interface has two deliberately different roles:

```text
HID = request and result records
CDC = receive-only diagnostic and physical evidence stream
```

HID proves that the Host protocol completed. CDC explains why that result is
accepted as physical execution rather than an echo or software calculation.

For every calculator round, CDC retained:

```text
[LIVE CPU ROUND] INTA=PASS commit=PASS EOI=PASS idle=PASS result=PASS
[NATIVE PROCESSOR ID] signature=0012 identity=INTEL 8086 result=PASS
```

It also retained the processor result witnesses:

```text
[NATIVE CALCULATOR] op=1 lhs=12   rhs=34  low=46    high=0  result=PASS
[NATIVE CALCULATOR] op=2 lhs=1000 rhs=7   low=993   high=0  result=PASS
[NATIVE CALCULATOR] op=3 lhs=300  rhs=200 low=60000 high=0  result=PASS
[NATIVE CALCULATOR] op=4 lhs=1000 rhs=33  low=30    high=10 result=PASS
```

The clean session ended with:

```text
INTEL 8086 heartbeat stopped: completed=73 lost=0 avg=2.7 ms
```

Raw evidence was saved at:

```text
D:\pi86-validation-logs\companion_heartbeat_20260828_043231+0800.log
D:\pi86-validation-logs\companion_heartbeat_20260828_043231+0800.json
```

The accepted evidence record is
[`../validation/host_loaded_internal_sram_calculator_1mhz_validation.md`](../validation/host_loaded_internal_sram_calculator_1mhz_validation.md).

## 8. Verify and retain the change

Before committing, Codex ran the repository checks:

```text
Markdown files checked = 106
Documentation failures = 0
DOCUMENTATION CHECK    = PASS

Ran 24 focused tests
OK

RP2350 firmware build
PASS

git diff --check
PASS
```

The source, tests, architecture updates, and physical evidence were committed
together as:

```text
9bf6299 Add Host-loaded Internal SRAM calculator execution
```

This makes the result reproducible and keeps the claim tied to exact artifacts
and retained physical evidence.

## What is automated now

With a connected and correctly assembled system, Codex can perform or direct:

- source inspection and implementation;
- native NASM workload generation;
- RP2350 firmware configuration and build;
- automated unit and documentation checks;
- Host-directed entry into UF2 boot mode;
- UF2 deployment when the boot volume is available;
- Host workload upload and lifecycle control;
- HID request/result exchange;
- CDC evidence collection and interpretation;
- acceptance/rejection based on observed results;
- documentation, commit, and push.

## What still requires physical availability or human action

The loop is automated, but it is not a robotic laboratory. It still depends on
the intended physical system being available. Human action is required for:

- assembling or rewiring hardware;
- soldering PSRAM or other components;
- inserting or replacing the Intel 8086 or NEC V30;
- correcting power, cable, or electrical faults;
- physically recovering a board when neither firmware nor USB control responds;
- authorizing actions that affect attached hardware or the remote repository.

Codex must also distinguish a build result from a physical result. If hardware
is absent, it may prepare artifacts and tests, but it cannot honestly declare a
physical gate `PASS`.

## Current boundary

The completed loop validates one bounded 16-byte workload. It is already a
genuine end-to-end development system, but it is not yet a general loader for
arbitrary 8086 binaries.

The next memory integration boundary is:

```text
general image layout
+ arbitrary native control flow
+ writable processor-visible RAM
+ stdio and fault/restart behavior
```

External PSRAM can later extend capacity behind the same Host workload
contract. It is not required to prove the core development loop.
