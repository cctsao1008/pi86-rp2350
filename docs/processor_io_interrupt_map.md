# Processor I/O and Interrupt Map

This document defines the processor-visible I/O-port and interrupt-vector ABI
shared by Intel 8086 and NEC V30 workloads and the RP2350 runtime.

The normative constants are mirrored for the two implementation languages:

- `firmware/runtime/processor_abi.h` for C firmware;
- `processor/include/rp86_abi.inc` for native NASM sources.

## I/O-port map

| Port | Name | Direction | Current contract |
|---:|---|---|---|
| `0020h` | PIC command | processor -> RP2350 | Prepared-runtime EOI/command write; accepted by the general responder |
| `00E0h` | Status | both | General reads return ready (`0001h`); writes are accepted for prepared-runtime compatibility |
| `00E2h` | TX | processor -> RP2350 | Prepared-runtime record transport; reserved in general workloads |
| `00E4h` | RX | RP2350 -> processor | Prepared-runtime record transport; not implemented by the general workload responder |
| `00E6h` | Control | processor -> RP2350 | `0001h` arms terminal `HLT` completion (`IDLE_PREPARE`) |
| `00E8h` | Result | processor -> RP2350 | Publishes one retained 16-bit native result |
| `00E9h` | Diagnostic stdout | processor -> RP2350 | Byte stream; CR/LF commits one `[NATIVE STDOUT]` evidence line |
| `00EAh` | Execution clock request | processor -> RP2350 | `0002h` confirms clock-stepped operation; `0001h` requests free-running and is currently rejected for general workloads |

An unlisted general-workload I/O cycle is a retained bus fault. Byte writes use
the physically selected lane; word writes publish the complete 16-bit value.

## Control values

| Interface | Value | Meaning |
|---|---:|---|
| Control `00E6h` | `0001h` | The next non-serviceable cycle is the workload's terminal `HLT`; transition `RUNNING -> COMPLETED` |
| Clock `00EAh` | `0001h` | Request `FREE_RUNNING` (reserved/rejected for current general workloads) |
| Clock `00EAh` | `0002h` | Request or confirm `CLOCK_STEPPED` |

`IDLE_PREPARE` is single-use and must be immediately followed by `HLT`. It is
not a general idle hint.

## Interrupt-vector map

| Vector | Name | Owner and use |
|---:|---|---|
| `20h` | Companion interrupt | RP2350 physical `INTR` plus two-cycle `INTA`; prepared runtime installs the handler |
| `60h` | Native service interrupt | Software service entry used by the prepared runtime and clock-transition validation |

The vector number and I/O port number `20h` occupy different architectural
namespaces and are not aliases. General workloads own their interrupt-vector
table in `00000h-003FFh` and must install every vector they enable.

## Completion sequence

```text
OUT 00E6h, 0001h   ; IDLE_PREPARE
HLT
        -> RP2350 accepts terminal HLT evidence
        -> workload state becomes COMPLETED
        -> final cycle count and native output remain retained
```

See also [`processor_memory_map.md`](processor_memory_map.md) for the memory and
mailbox address spaces.
