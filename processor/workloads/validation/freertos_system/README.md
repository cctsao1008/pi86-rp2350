# FreeRTOS system workload

This is the Issue #67/#61 system-level workload. It is deliberately separate
from `freertos_port/`, which remains the closed Issue #60 portable-layer
acceptance witness.

The workload runs three tasks on the physical Intel 8086 using the already
validated RP86 FreeRTOS/8086 port:

- `LED`: toggles a RAM-backed virtual LED every 500 ms with `vTaskDelay()`;
- `PROD`: sends a monotonically increasing 16-bit value to a queue every 300 ms;
- `CONS`: blocks on that queue and verifies sequence continuity.

There is no RP2350 GPIO LED in this proof. The physical 8086 owns the state;
the Host/Web UI may only observe and render it.

## Telemetry

`gRp86Telemetry` is an ordinary workload global in processor-visible Internal
SRAM. Its address is workload-local and must be discovered from the generated
linker map; no permanent telemetry address or public ABI is claimed yet.

The first layout is intentionally only 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| `00h` | 2 | `led_state` (`0`/`1`) |
| `02h` | 2 | `led_toggle_count` |
| `04h` | 2 | `queue_tx_count` |
| `06h` | 2 | `queue_rx_count` |
| `08h` | 2 | `last_event.seq` |
| `0Ah` | 2 | `last_event.event` |
| `0Ch` | 2 | `last_event.arg` |
| `0Eh` | 2 | `error_count` |

Event IDs are workload-local:

```text
1  BOOT
2  LED_ON
3  LED_OFF
4  QUEUE_SEND
5  QUEUE_RECV
6  ERROR
```

`last_event.seq` doubles as a tiny seqlock for the complete 16-byte telemetry
block. The processor publishes an odd value before mutation and an even value
after `event`/`arg` and the other telemetry fields are complete. A Host reader
accepts a snapshot only when the sequence read before and after the block is the
same even value. This adds one 16-bit write at update start and avoids locks,
ring buffers, timestamps, printf, or a separate logging transport.

The reserved `3F000h-3FFFFh` ownership-transfer mailbox is not used as scratch
telemetry storage.

## Build

Use the canonical build entry point:

```text
python3 scripts/build.py freertos-system
```

The package is generated as:

```text
build-freertos-system/workloads/FREERTOS-SYSTEM.P86W
```

The generated map is:

```text
build-freertos-system/processor/generated/freertos_system_validation/
    freertos_system_validation.map
```

The Host telemetry helper resolves `_gRp86Telemetry` directly from that map, so
normal validation does not require copying or freezing a physical address.

With an RP86 runtime/broker already active, read one coherent snapshot without
stopping the workload:

```text
py tools/rp86_freertos_status.py --map build-freertos-system/processor/generated/freertos_system_validation/freertos_system_validation.map
```

For the Issue #71 sustained-run witness, collect multiple snapshots and require
forward progress:

```text
py tools/rp86_freertos_status.py --map build-freertos-system/processor/generated/freertos_system_validation/freertos_system_validation.map --samples 6 --interval 1 --verify-progress
```

The helper performs only ordinary processor-visible RAM reads. `--verify-progress`
passes only when LED, queue TX/RX, and event counters advance between the first
and last samples while `error_count` remains zero. A literal telemetry address
is still accepted as a diagnostic override, but the linker map is the canonical
discovery source.

## Restart contract

RP86 `workload restart` re-enters the same RAM-resident C/16 workload image;
it does not perform a new Host upload. The workload startup clears ordinary
BSS, but FreeRTOS also defines explicit module reset APIs for restarting the
scheduler in an existing C runtime image.

Before allocating any queue, TCB, or task stack, the system workload therefore
performs:

```c
vTaskResetState();
vPortHeapResetState();
```

`vTaskResetState()` returns the FreeRTOS task/scheduler module to its startup
state, including `pxCurrentTCB`, task counts, tick/scheduler state, and list
bookkeeping. `vPortHeapResetState()` resets the `heap_1` allocator state. Both
calls are also valid on the first cold entry, where those modules are already
in their initial state.

This is a workload/runtime restart requirement, not an Intel-8086-specific
kernel modification. The upstream FreeRTOS kernel remains unchanged.

## Runtime expectation

A healthy workload is long-running. It does not publish a terminal PASS or
execute HLT during normal operation. Acceptance is sustained forward progress:

```text
LED state/count changes
queue_tx_count advances
queue_rx_count follows and advances
last_event.seq advances and is even when stable
error_count remains 0
```

Host `status`, `stop`, and `restart` remain the lifecycle controls. A fatal
creation, queue, sequence, or assertion failure publishes a `67xx`/`6Fxx`
result, emits `RESULT: FAIL`, and terminates through the ordinary RP86 idle/HLT
path.

Intel 8086 is the primary physical acceptance target. NEC V30 remains the
final compatibility regression under Issue #61 and must use the same common
8086 baseline rather than V30-specific fixes.
