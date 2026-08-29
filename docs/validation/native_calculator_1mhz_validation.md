# Native Calculator Service — 1 MHz Physical Intel 8086 Validation

## Result

**PASS** — on 2026-08-27, the RP86 shell sent four calculator requests through
the composite HID runtime to a physical Intel `P8086-2`. The processor executed
the selected native arithmetic instruction inside its interrupt service,
committed the result, issued EOI, returned through `IRET`, and continued its
heartbeat with zero loss during the retained validation session.

This validates a bounded interactive native service. It does not claim that
general arbitrary-address Internal-SRAM workload launch is complete.

## Physical configuration

- Processor: Intel `P8086-2`, automatically identified by native `AAD 16`
- Processor clock: 1.000 MHz
- Host transport: 64-byte USB HID records
- Evidence transport: receive-only USB CDC
- Runtime path: Host -> RP2350 prepared state -> PIO/DMA -> physical bus
- Interrupt path: physical `INTR` and two-cycle `INTA`
- Current-cycle M33 response: none

Firmware artifact:

```text
rp86_native_calculator_1mhz.uf2
SHA-256 f9a0ae207ca7c6145e94dc08fcbb48ba49c7ba288958482fc1df5a922113be5e
```

Native ROM identity:

```text
451 bytes
SHA-256 4f5ebae3aff6a7f464529b24214aced1538c4abb01472d888896374427f2f5b9
```

## Host-visible exchanges

```text
[019] CALC 12+34=46       latency=2.2 ms
[030] CALC 1000-7=993     latency=2.4 ms
[040] CALC 300*200=60000  latency=2.6 ms
[052] CALC 1000/33=30 R10 latency=3.8 ms
```

The clean retained session ended with:

```text
INTEL 8086 ALIVE=True completed=63 lost=0 min/avg/max=2.2/3.0/4.2 ms
boot_id=1 cpu_seq=329 command_seq=0
INTEL 8086 heartbeat stopped: completed=74 lost=0 avg=3.0 ms
```

## CDC physical evidence

The four calculator transactions were retained as native records:

```text
[NATIVE CALCULATOR] op=1 lhs=12   rhs=34  low=46    high=0  result=PASS
[NATIVE CALCULATOR] op=2 lhs=1000 rhs=7   low=993   high=0  result=PASS
[NATIVE CALCULATOR] op=3 lhs=300  rhs=200 low=60000 high=0  result=PASS
[NATIVE CALCULATOR] op=4 lhs=1000 rhs=33  low=30    high=10 result=PASS
```

Every corresponding `[LIVE CPU ROUND]` record reported:

```text
INTA=PASS commit=PASS EOI=PASS idle=PASS result=PASS
```

Every round also retained the physical processor identity witness:

```text
[NATIVE PROCESSOR ID] signature=0012 identity=INTEL 8086 result=PASS
```

Raw Host evidence:

```text
D:\pi86-validation-logs\companion_heartbeat_20260827_085040+0800.log
D:\pi86-validation-logs\companion_heartbeat_20260827_085040+0800.json
```

## Execution ownership

Python parses syntax and serializes operands; it does not calculate the answer.
The RP2350 selects one two-byte native instruction before asserting `INTR`:

| Operation | Native instruction | Opcode word |
|---|---|---|
| addition | `ADD AX,CX` | `C801h` |
| subtraction | `SUB AX,CX` | `C829h` |
| multiplication | `MUL CX` | `E1F7h` |
| division | `DIV CX` | `F1F7h` |

The physical processor receives operands in registers, executes that
instruction, and returns its result words through the native mailbox. The
RP2350 verifies the operation, operands, result layout, processor identity,
commit, EOI, and return-to-idle witnesses before sending the Host reply.

## Safety and scope

- The instruction layout and fetch addresses remain fixed; only the prepared
  two-byte instruction value changes.
- The calculator does not add an M33 current-cycle lookup.
- Invalid syntax and division by zero are rejected before physical dispatch.
- Normal heartbeat resumed after each command and `cpu_seq` continued to rise.
- General staged-image execution remains a separate open integration item.
