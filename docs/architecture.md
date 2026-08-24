# V30 Companion-Chip Architecture

## 1. Scope

`pi86-rp2350` builds a programmable companion chipset around a **physical NEC V30** using the Waveshare RP2350-PiZero and the original Pi86 V20/V30 HAT.

The V30 remains the processor executing native code. The RP2350 supplies the surrounding timing, memory, I/O, interrupt, observation, control, and host-service functions.

The architecture is based on one boundary:

> **PIO/DMA and bounded on-chip state own current-cycle V30 timing. Arm software, host tools, storage, and AI operate around that realtime path.**

The Raspberry Pi-compatible 40-pin physical header remains the hardware ABI. See [`hardware_contract.md`](hardware_contract.md).

## 2. System model

```text
Host tools / AI
       |
Observe / Control / Experiment
       |
     RP2350
  PIO / DMA / services
       |
 Original Pi86 HAT
       |
 Physical NEC V30
```

The RP2350 is not treated as a faster host running a software polling loop. Its responsibilities are split into three architectural layers:

- **Deterministic bus plane** - current-cycle capture, qualification, response, clock/phase control, and DMA transport.
- **Control/service plane** - machine state, device services, supervision, USB-facing functions, image management, and non-critical work.
- **Host interface** - structured observation, bounded control, repeatable experiments, and conventional or AI-assisted tooling.

Exact PIO state-machine placement or Core 0/Core 1 assignment may change without changing these ownership rules.

## 3. Deterministic bus plane

### 3.1 Passive capture

PIO observes V30 address, control, and data phases without taking ownership of the AD bus. Captured state can be retained through DMA for later decoding or analysis.

### 3.2 Qualified response

A responder may drive AD0-AD15 only for a qualified physical cycle. Bus direction, drive enable, response data, and release are part of the deterministic response path rather than ordinary software GPIO activity.

Unsupported or late responses must remain high-Z rather than expose stale or speculative data.

### 3.3 Clock and phase

PIO owns V30 clock generation and the phase relationships used by deterministic response engines. Clock generation is part of the physical machine configuration, not a software delay mechanism.

### 3.4 DMA transport

DMA moves prepared state between SRAM and PIO FIFOs and retains captured observations. DMA is a transport mechanism; it does not make an Arm core part of an active V30 response cycle.

## 4. Arm control and service roles

Arm software prepares, supervises, and consumes state around the deterministic bus plane.

A **realtime-control role** may:

- supervise bus epochs and deterministic engines;
- prepare immutable response state outside the active cycle;
- manage bounded queues and ownership transfer;
- capture writes or exceptional events;
- detect starvation, deadline, or transport failures.

An **asynchronous-service role** may:

- provide USB console and host communication;
- manage ROM/test images and configuration;
- process retained traces;
- perform filesystem or storage work;
- provide debugging and higher-level machine services.

These are architectural roles rather than permanent core numbers. Detailed inter-core ownership is defined in [`dual_core_partitioning.md`](dual_core_partitioning.md).

## 5. Memory and machine services

The V30 exposes a 20-bit physical address space. The RP2350 separates deterministic hot state from slower backing resources.

- **Internal SRAM** holds deterministic response state, descriptors, hot ROM/RAM windows, mailbox state, device state, and PIO/DMA queues.
- **External PSRAM** is bulk backing/workspace for writable state, traces, snapshots, images, or service buffers.
- **Flash and external/host storage** hold persistent firmware, ROMs, disk images, and workload assets outside the active-cycle path.

The original Pi86 HAT keeps V30 `READY` asserted, so arbitrary slow backing access cannot be assumed to complete a current-cycle response. A resource must either be represented by a bounded deterministic hit or remain outside the active response path.

See [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md).

Memory and I/O backends are separated from bus timing:

```text
physical V30 transaction
          |
  classify / qualify
          |
   +------+------+
   |             |
 memory        I/O/device
   |             |
ROM / RAM    PIC / PIT / mailbox / ...
```

The bus layer owns timing, lane semantics, direction, and drive qualification. Memory and device backends own address/port semantics, not GPIO timing.

## 6. Observation, control, and experiment interface

The host-facing architecture exposes three generic capabilities.

### Observe

Provide machine-readable V30-visible and RP2350-visible state such as bus activity, memory/I/O transactions, interrupt activity, response class, timing metadata, and runtime/device state.

### Control

Provide bounded machine operations outside the current-cycle timing path, such as machine reset/run control, supported clock configuration, image selection, trace configuration, interrupt/test requests, and state queries.

### Experiment

Provide a repeatable host workflow that can configure a test, execute it on the physical V30, retain structured results, compare runs, and pass the same data to scripts or AI-assisted analysis.

AI is therefore a host-side client, not a bus-timing component. Provider-specific behavior remains above the machine interface.

See [Issue #50](https://github.com/cctsao1008/pi86-rp2350/issues/50), [`ai_bridge_architecture.md`](ai_bridge_architecture.md), and [`companion_service_abi.md`](companion_service_abi.md).

## 7. Compatibility profiles and workloads

PC-class behavior is optional rather than the definition of the architecture.

The same physical V30/RP2350 platform can host increasingly complex workloads such as:

- native diagnostic ROMs and assembly tests;
- 8259A-compatible interrupt services;
- 8253/8254-class timer services;
- BIOS workloads;
- DOS, ELKS, or other native V30 software;
- optional storage, display, keyboard, or other PC-class services.

These workloads exercise the programmable chipset but do not require the project to become a conventional PC/XT clone.

## 8. Existing-HAT timing boundary

The project retains the original Pi86 HAT as the working hardware baseline.

The scattered V30 AD0-AD15 mapping is handled by encoded GPIO words and PIO pin ownership. Validated PIO-direct response work established that this physical mapping can be driven without an M33 current-cycle round trip.

The existing `READY` connection remains a hard architectural constraint: the software-defined chipset must design around deterministic hits and safe unsupported cycles rather than assuming wait-state insertion.

A replacement board is not part of the current architecture unless a demonstrated blocker makes reconsideration necessary. Older replacement-board documents remain historical design records.

## 9. Implementation lineage

The repository contains both early software-stepped mechanisms and newer PIO/DMA deterministic mechanisms.

The software-stepped path remains useful for functional characterization and historical bring-up. It is not the architectural model for current-cycle continuous-clock response.

The current architecture derives from the validated progression from reset/fetch and software-stepped memory/I/O semantics to PIO-direct clocking, qualified response, interrupt handling, descriptor-fed ROM execution, persistent runtime, and host bridge experiments. Exact measured results belong in [`validation/`](validation/) and historical bring-up records rather than this architecture document.

## 10. Related documents

- [`hardware_contract.md`](hardware_contract.md) - physical interface contract
- [`pin_mapping.md`](pin_mapping.md) - GPIO/header/V30 signal mapping
- [`dual_core_partitioning.md`](dual_core_partitioning.md) - realtime/service ownership
- [`companion_service_abi.md`](companion_service_abi.md) - host and V30-visible ABI
- [`ai_bridge_architecture.md`](ai_bridge_architecture.md) - provider-neutral host bridge
- [`adr/0002-adopt-v30-companion-chip-architecture.md`](adr/0002-adopt-v30-companion-chip-architecture.md) - companion-chip architecture decision
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - deterministic memory policy
