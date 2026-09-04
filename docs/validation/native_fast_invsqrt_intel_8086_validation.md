# Native Fast Inverse Square Root — Physical Intel 8086 Validation

## Result

**PASS — 2026-08-30.** A physical Intel `P8086-2` executed the RP86
`INVSQRT.P86W` workload from Internal SRAM under `CLOCK_STEPPED` control. The
native program completed five deterministic Q8.8 inverse-square-root vectors,
reported `RESULT: PASS`, requested cooperative idle, and entered `HLT` after
3,212 processor cycles.

The arithmetic ran on the installed processor. The Host loaded and controlled
the image; the RP2350 owned Internal SRAM, the physical bus, clocking, and the
diagnostic/control ports.

## Accepted artifacts

```text
Native image: 623 bytes
SHA-256: 617a3669e60596d9350e69b6a5c32a0055d8d09209150987ba07575ff998feae

INVSQRT.P86W: 663 bytes
SHA-256: e0728ee058583d1209cb41a190d1bd00ba721e45f43ad9edae5b466b3440631e
CRC32 of uploaded image: 52AECF49
```

Workload placement:

```text
load address  10000h
entry         1000:0000
memory        RP2350 Internal SRAM
clock         CLOCK_STEPPED
```

## Native output retained through CDC

```text
[NATIVE STDOUT] 8086 FAST INVSQRT Q8.8
[NATIVE STDOUT] T1 X=0100 Y=00FF E=0100 PASS
[NATIVE STDOUT] T2 X=0200 Y=00B5 E=00B5 PASS
[NATIVE STDOUT] T3 X=0400 Y=007F E=0080 PASS
[NATIVE STDOUT] T4 X=0900 Y=0055 E=0055 PASS
[NATIVE STDOUT] T5 X=1000 Y=003F E=0040 PASS
[NATIVE STDOUT] RESULT: PASS
[WORKLOAD IDLE] armed native HLT indication accepted
```

The one-LSB differences for 1, 4, and 16 are inside the workload's declared
Q8.8 acceptance window.

Final Host-visible state before the controlled stop:

```text
Workload   RUNNING id=4 detail=623
Clock mode CLOCK-STEPPED
Bus cycles 3212
Processor  IDLE / HLT
Load       0x10000 entry=1000:0000
```

The Host then issued `stop`; RP2350 returned the processor to `RESET=HIGH`,
`CLK=LOW`, and reported `STOPPED / RESET` without increasing the accepted
cycle count.

Raw Host evidence:

```text
D:\pi86-validation-logs\companion_heartbeat_20260830_195157+0800.log
D:\pi86-validation-logs\companion_heartbeat_20260830_195157+0800.json
```

## Pre-acceptance defects found by physical execution

The first build exposed a NASM 3.02 flat-binary relocation error for data-table
addresses. Image-relative offsets fixed the build without weakening warnings.

The first physical run completed `T1` and then lost control flow. Disassembly
showed that NASM had expanded a distant `jnz` into `0F 85 rel16`, a near
conditional jump introduced after the 8086. The installed 8086 therefore could
not execute the generated instruction.

The accepted source now declares `cpu 8086` and expresses the long conditional
transfer as an 8086-compatible short condition followed by a near `JMP`.
Consequently, future use of an instruction above the target ISA becomes an
assembly error rather than a physical-runtime surprise.

## Scope

This validation proves native fixed-point lookup, multiplication, shifting,
Newton-Raphson refinement, loops, diagnostic output, cooperative idle, and
lifecycle stop on the physical Intel 8086. It does not yet compare NEC V30
timing or validate the optional External PSRAM tier.
