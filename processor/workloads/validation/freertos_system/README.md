# FreeRTOS system workload

This is the Issue #67/#61 system-level workload.  It is deliberately separate
from `freertos_port/`, which remains the closed Issue #60 portable-layer
acceptance witness.

The workload runs three tasks on the physical Intel 8086 using the already
validated RP86 FreeRTOS/8086 port:

- `LED`: toggles a RAM-backed virtual LED every 500 ms with `vTaskDelay()`;
- `PROD`: sends a monotonically increasing 16-bit value to a queue every 300 ms;
- `CONS`: blocks on that queue and verifies sequence continuity.

There is no RP2350 GPIO LED in this proof.  The physical 8086 owns the state;
the Host/Web UI may only observe and render it.

## Telemetry

`gRp86Telemetry` is an ordinary workload global in processor-visible Internal
SRAM.  Its address is workload-local and must be discovered from the generated
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
block.  The processor publishes an odd value before mutation and an even value
after `event`/`arg` and the other telemetry fields are complete.  A Host reader
accepts a snapshot only when the sequence read before and after the block is the
same even value.  This adds one 16-bit write at update start and avoids locks,
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

Locate `_gRp86Telemetry` in that map and convert its segment:offset to a physical
address.  This discovery step is intentional for the first version so the
experiment does not prematurely freeze a telemetry address.

With an RP86 runtime/broker already active, read and decode one coherent snapshot
without stopping the workload:

```text
py tools/rp86_freertos_status.py <physical-address>
```

The helper performs only ordinary processor-visible RAM reads.  The 8086 remains
the source of truth and the Host only decodes the 16-byte witness.

## Runtime expectation

A healthy workload is long-running.  It does not publish a terminal PASS or
execute HLT during normal operation.  Acceptance is sustained forward progress:

```text
LED state/count changes
queue_tx_count advances
queue_rx_count follows and advances
last_event.seq advances and is even when stable
error_count remains 0
```

Host `status`, `stop`, and `restart` remain the lifecycle controls.  A fatal
creation, queue, sequence, or assertion failure publishes a `67xx`/`6Fxx`
result, emits `RESULT: FAIL`, and terminates through the ordinary RP86 idle/HLT
path.

Intel 8086 is the primary physical acceptance target.  NEC V30 remains the
final compatibility regression under Issue #61 and must use the same common
8086 baseline rather than V30-specific fixes.
