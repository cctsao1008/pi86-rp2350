# Processor I/O and Interrupt Map

This document defines the processor-visible I/O-port and interrupt-vector ABI
shared by Intel 8086 and NEC V30 workloads and the RP2350 runtime.

The normative constants are mirrored for the two implementation languages:

- `firmware/runtime/processor_abi.h` for C firmware;
- `processor/include/rp86_abi.inc` for native NASM sources.

## I/O-port map

| Port | Name | Direction | Current contract |
|---:|---|---|---|
| `0020h` | PIC command | processor -> RP2350 | `20h` acknowledges/EOIs the active RP86 interrupt source; prepared-runtime command writes remain compatible |
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
| PIC command `0020h` | `0020h` | EOI the current RP86 interrupt. For the periodic tick this clears the in-service state and allows the retained next tick to assert. |
| Control `00E6h` | `0001h` | The next non-serviceable cycle is the workload's terminal `HLT`; transition `RUNNING -> COMPLETED` |
| Clock `00EAh` | `0001h` | Request `FREE_RUNNING` (reserved/rejected for current general workloads) |
| Clock `00EAh` | `0002h` | Request or confirm `CLOCK_STEPPED` |

`IDLE_PREPARE` is single-use and must be immediately followed by `HLT`. It is
not a general idle hint.

## Interrupt-vector map

| Vector | Name | Owner and use |
|---:|---|---|
| `20h` | Companion interrupt | RP2350 physical `INTR` plus two-cycle `INTA`; prepared runtime installs the handler |
| `21h` | Periodic RTOS tick | Optional general-workload interrupt. The workload installs the IVT entry at `0000:0084/0086`; RP2350 supplies vector `21h` on INTA #2. |
| `60h` | Native service interrupt | Software service entry used by the prepared runtime and clock-transition validation |

The vector number and I/O port number `20h` occupy different architectural
namespaces and are not aliases. General workloads own their interrupt-vector
table in `00000h-003FFh` and must install every vector they enable.

## Periodic tick contract

A `.P86W` workload opts in with the `periodic-tick` capability flag. The
initial implementation is intentionally limited to `CLOCK_STEPPED` execution.
The RP2350 generates a **100 Hz wall-clock tick** (10 ms period) and asserts
physical `INTR` only between complete processor bus cycles.

The Intel 8086 two-cycle acknowledge contract is explicit:

```text
INTA #1  -> interrupt accepted; RP2350 deasserts INTR; AD remains high-Z
INTA #2  -> RP2350 drives vector 21h on the low data byte
ISR      -> workload executes handler
OUT 20h,20h -> EOI clears the tick in-service state
IRET     -> workload resumes interrupted execution
```

At most one tick request is pending. If additional wall-clock periods elapse
while a request is already pending, being acknowledged, or in service, RP2350
does not queue an interrupt storm. It retains one request and counts the
additional periods as coalesced evidence. A request that became pending while
a previous tick was in service reasserts at the first complete bus-cycle
boundary after EOI; it does not wait for the following 10 ms deadline.

For the initial FreeRTOS path, the idle task must use spin/yield rather than
`HLT`. The periodic tick does not change the terminal workload contract below.

## Completion sequence

```text
OUT 00E6h, 0001h   ; IDLE_PREPARE
HLT
        -> RP2350 accepts terminal HLT evidence
        -> workload state becomes COMPLETED
        -> final cycle count, native output, and tick counters remain retained
```

See also [`processor_memory_map.md`](processor_memory_map.md) for the memory and
mailbox address spaces.
