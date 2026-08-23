# V30 Companion-Chip Architecture

## Goal

Build a programmable chipset around the physical NEC V30 and original Pi86 HAT using the Waveshare RP2350-PiZero.

The RP2350 is not treated as a faster Raspberry Pi running a Pi86-style polling loop. Its responsibilities are separated into a deterministic bus data plane and slower system services:

```text
BIOS / monitor / applications
             |
             v
         NEC V30
             |
      multiplexed x86 bus
             |
             v
 +-------------------------------+
 |            RP2350             |
 |                               |
 | PIO + DMA bus data plane      |
 | address/control supervision   |
 | ROM / RAM / I/O device model  |
 | debugger and system services  |
 +------+-------+------+---------+
        |       |      |
      PSRAM   MicroSD  USB/DVI
```

The physical Raspberry Pi 40-pin header position remains the hardware ABI. The HAT is used without signal reassignment. See [`hardware_contract.md`](hardware_contract.md).

## Evidence that selected this architecture

The software-stepped gates established the functional V30 contract:

- reset and first fetch at `0xFFFF0`;
- executable SRAM-backed ROM and far jump to `0xF0000`;
- RAM read/write and CPU-semantic branches;
- byte lanes and odd-address words;
- I/O transactions;
- PIC, interrupt acknowledge, IVT, ISR, EOI, and `IRET`;
- programmable PIT channel 0 to IRQ0.

PC1-B then established the high-speed physical response mechanism. A pre-staged `EB FE` self-loop passed from 0.300 through 8.000 MHz configured V30 clock using:

```text
encoded SRAM words -> DMA -> PIO1 TX FIFO
                              |
                              v
                   OUT pins, 28 + MOV PINDIRS
                              |
                              v
                    scattered AD0-AD15 GPIOs
```

The superseded DMA-to-SIO experiment is not part of the architecture. SIO is processor-local on RP2350 and cannot be the DMA response destination.

## Runtime partitioning

### Deterministic data plane

- **PIO clock engine:** generates the continuous V30 clock and can stop it LOW at controlled boundaries.
- **PIO address/control capture:** observes ASTB/T1 and the relevant bus-control state without driving the bus.
- **PIO read responder:** owns encoded AD output and `PINDIRS` assertion/release during qualified response windows.
- **DMA:** moves prepared words between SRAM and PIO FIFOs; it does not write SIO.

### Real-time supervision

One M33 role is reserved for work that must remain close to the bus but cannot be expressed entirely in PIO/DMA:

- decode captured scattered GPIO snapshots into V30 addresses and cycle types;
- maintain deterministic ROM/RAM cache and response queues;
- capture writes and exceptional cycles;
- record deadline misses and starvation;
- coordinate PIO/DMA without blocking on service-layer locks.

The exact Core 0/Core 1 assignment remains provisional until contention and interrupt behavior are measured.

The normative workload and inter-core communication rules are defined in
[`dual_core_partitioning.md`](dual_core_partitioning.md). In particular, the
real-time M33 role supervises and prepares future responses; it is not assumed
to complete a no-wait current V30 cycle. That hard deadline remains owned by
PIO/DMA unless a separately validated READY-based contract applies.

### Service plane

The other M33 role owns work that must not block a V30 response:

- USB CDC debugger and diagnostic console;
- ROM, disk, and configuration images;
- MicroSD access;
- keyboard translation;
- display/CGA rendering and DVI;
- monitor and host-side control commands.

Communication between the real-time and service roles should use bounded, lock-free queues or explicit ownership transfer. Slow service completion is never assumed to meet a bus-cycle deadline.

The accepted AI Bridge target uses this service boundary for a Codex-first
conversation with the physical V30. A provider-neutral Host Bridge terminates
USB HID messages, while provider-specific behavior remains in host-side
adapters. The RP2350 service and realtime roles exchange complete conceptual
`HOST_TO_V30_RECORD` and `V30_TO_HOST_RECORD` objects; the V30 bus consumes
only locally staged state.

The V30-visible abstraction is a companion service expressed through ordinary
I/O, memory, polling, interrupts, and native code. It is not an AI abstraction.
See [`ai_bridge_architecture.md`](ai_bridge_architecture.md) for the canonical
translation boundary, message ownership, and evidence model.

## Scattered AD bus strategy

The original HAT maps V30 AD0-AD15 across scattered RP2350 GPIOs. PC1-B proved the following approach:

1. Encode each V30 word into a GPIO0-27 bitmap in SRAM.
2. Configure the PIO1 OUT group as the contiguous GPIO0-27 window.
3. Function-mux only the sixteen AD pins to PIO1.
4. Leave intervening control and clock pins assigned to their own functions.
5. Use `OUT PINS, 28` to stage data.
6. Use RP2350 `MOV PINDIRS, ~NULL` and `MOV PINDIRS, NULL` for ownership and release.

Default input synchronizers remain enabled. Bypass is an optimization experiment, not part of the validated baseline.

## Address-qualified memory boundary

PC1-B consumed a fixed response stream. A real companion chip must select data from the captured bus transaction:

```text
ASTB/T1 snapshot
   |
   +-> decode A19:A0
   +-> decode IO/M, BUFR/W, UBE, A0
   |
   v
qualified memory read
   |
   v
ROM/RAM lookup or deterministic cache hit
   |
   v
encoded response -> PIO1 TX FIFO -> AD bus
```

PC1-C0 is accepted only when the V30 executes a far jump from `FFFF0` into an address-qualified ROM target and reaches a CPU-visible checkpoint. A transaction-count-indexed response stream is useful diagnostic evidence but is not accepted as a general ROM backend.

## READY and deadline policy

The current HAT connects V30 `READY` to 3.3 V. RP2350 cannot insert a wait state on the existing interface.

Consequences:

- every qualified response has a hard deadline;
- MicroSD and general PSRAM access cannot sit directly in the cycle-critical path without a deterministic cache contract;
- cache misses must be prevented, predicted, handled at a lower clock, or supported by a future hardware revision;
- the first dynamic-lookup failure frequency must be recorded rather than hidden by a fixed-response result.

## Memory and I/O layering

```text
physical V30 transaction
          |
          v
bus-cycle classification and lane decode
          |
          +--------------------+
          |                    |
          v                    v
address-qualified memory     I/O dispatcher
          |                    |
     +----+----+          +----+----------------+
     |         |          | PIC | PIT | debug | ...
     v         v
    ROM       RAM
```

Rules:

- the bus layer owns timing, direction, and lane semantics;
- memory backends own bytes and address ranges, not GPIO timing;
- device backends own port semantics, not PIO state;
- AD remains high-Z outside a qualified response window;
- writes are captured without enabling the RP2350 AD drivers;
- unsupported reads have an explicit policy and may not accidentally drive stale data.

## Software progression

The first observable firmware service should be a diagnostic output port rather than a complete display stack. A minimal ROM can execute `OUT 0E9h, AL`; the RP2350 then mirrors those bytes to USB CDC. This proves ROM-to-CPU-to-I/O flow before UART, OLED, CGA, or BIOS console compatibility is introduced.

Recommended sequence:

```text
PC1-C0  address-qualified far-jump ROM
PC1-C1  debug-port Mini BIOS signature
PC1-D   deterministic RAM backend
PC1-E   ROM monitor
PC1-F   minimum BIOS services
PC1-G   boot-sector / DOS or CP/M-86 exploration
```

## Validation rules

- State whether a result is fixed-response, address-qualified, cached, or general.
- Prefer V30-visible control-flow or data checkpoints over DMA counters alone.
- Preserve the post-reset clean epoch and safe terminal state.
- Re-run the software-stepped semantic gates when their backends are moved behind the continuous-clock engine.
- Do not claim full 8 MHz operation until ROM, RAM, byte lanes, I/O, interrupt acknowledge, and sustained service are integrated at that clock.

## Decision record

See [`adr/0002-adopt-v30-companion-chip-architecture.md`](adr/0002-adopt-v30-companion-chip-architecture.md).
