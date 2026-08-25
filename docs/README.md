# pi86-rp2350 Documentation

The documentation is organized around one current definition:

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**

Historical experiments remain available as evidence, but they do not define the
current architecture.

## Start here

1. [`architecture.md`](architecture.md) — canonical architecture and role boundaries
2. [`host_runtime_architecture.md`](host_runtime_architecture.md) — detailed runtime, permissions, services, and implementation gates
3. [`host_runtime_shell.md`](host_runtime_shell.md) — remote-shell command model
4. [`memory_architecture.md`](memory_architecture.md) — Internal SRAM, PSRAM, NOR, SD, FAT, and shared ownership
5. [`host_protocol.md`](host_protocol.md) — language-independent Host/RP2350 operations
6. [`hardware_contract.md`](hardware_contract.md) — physical signal and ownership contract

The three roles are:

```text
Host      = Runtime Controller
RP2350    = Companion Resource and Bus Controller
8086/V30  = Bare-Metal Remote Physical Processor
```

## Current architecture and interfaces

| Document | Authority |
|---|---|
| [`architecture.md`](architecture.md) | Project identity, roles, runtime, and boundaries |
| [`host_runtime_architecture.md`](host_runtime_architecture.md) | Detailed resource, permission, workload, service, and failure model |
| [`host_runtime_shell.md`](host_runtime_shell.md) | Host command framework |
| [`memory_architecture.md`](memory_architecture.md) | Shared memory/storage ownership and public volume names |
| [`host_protocol.md`](host_protocol.md) | Typed Host operations and transport-independent semantics |
| [`companion_service_abi.md`](companion_service_abi.md) | Existing 64-byte record and V30 mailbox ABI |
| [`dual_core_partitioning.md`](dual_core_partitioning.md) | RP2350 realtime and service-role partition |
| [`hardware_contract.md`](hardware_contract.md) | Current hardware contract |
| [`pin_mapping.md`](pin_mapping.md) | Pi86 HAT to RP2350 signal mapping |
| [`adr/0008-adopt-host-managed-bare-metal-processor-runtime.md`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) | Current architecture decision |
| [`adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md`](adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md) | Intel 8086 / NEC V30 processor scope |

## Host communication

- [`ai_bridge_architecture.md`](ai_bridge_architecture.md) describes the
  provider-neutral Host bridge and the validated AI greeting as one application.
- [`development/windows_physical_validation.md`](development/windows_physical_validation.md)
  documents Windows-side physical validation.
- The Python tools under [`../tools/ai_bridge/`](../tools/ai_bridge/) are
  reference clients, not mandatory architecture layers.

AI is simply a possible Host client. The V30 sees mailbox, stdio, file, memory,
and interrupt services—not prompts, models, or providers.

## Hardware and development

- [`hardware.md`](hardware.md) — physical platform notes
- [`pi86_hat_design_review.md`](pi86_hat_design_review.md) — original HAT review
- [`bringup.md`](bringup.md) — physical bring-up and acceptance
- [`bringup/recovery.md`](bringup/recovery.md) — recovery procedures
- [`development/build_and_toolchain.md`](development/build_and_toolchain.md) — build workflow
- [`toolchain.md`](toolchain.md) — toolchain reference
- [`../tools/docs/README.md`](../tools/docs/README.md) — documentation checks

## Validation and history

- [`validation/`](validation/) contains accepted physical validation evidence.
- [`validation/canonical_runtime_integration_1mhz_validation.md`](validation/canonical_runtime_integration_1mhz_validation.md)
  records the canonical 1 MHz CDC+HID integration on a physical Intel 8086.
- [`validation/intel_8086_interactive_heartbeat_1mhz_observation.md`](validation/intel_8086_interactive_heartbeat_1mhz_observation.md)
  records the first Intel P8086-2 interactive heartbeat observation.
- [`bringup/gate_history.md`](bringup/gate_history.md) records the gate sequence.
- [`story/`](story/) preserves the public development story.
- [`retrospectives/`](retrospectives/) records lessons learned.
- [`releases/`](releases/) contains historical milestones.
- [`archive/`](archive/) holds superseded plans and former project directions.

Validation files preserve their original clocks, terminology, outputs, and
terminal states. A later architecture does not rewrite an earlier measurement.

## Current versus archived material

The main documentation tree describes the Host-managed runtime or current
hardware/validation facts. BIOS-first, PC-machine, ELKS boot, replacement-board,
and former terminology documents are archived because they no longer define the
project.

Archive status does not mean those experiments were wrong. It means their active
architectural role was superseded.

## Documentation rule

Put stable project structure in the canonical contracts, current implementation
work in issues, accepted measurements in `validation/`, and superseded
directions in `archive/`. Avoid duplicating the project definition in every
document; link to [`architecture.md`](architecture.md).
