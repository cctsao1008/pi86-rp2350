# pi86-rp2350 Documentation

This directory contains the detailed architecture, interface contracts, engineering notes, implementation plans, and historical validation records for `pi86-rp2350`.

The top-level [`README.md`](../README.md) is the public project overview. Documents here carry implementation and engineering detail that does not belong on the repository front page.

## Start here

1. [`architecture.md`](architecture.md) - canonical Host / RP2350 / physical-V30 machine architecture
2. [`memory_architecture.md`](memory_architecture.md) - physical memory resources, V30 Memory Map, and backing policy
3. [`host_protocol.md`](host_protocol.md) - canonical language-independent HID/CDC Host Protocol
4. [`hardware_contract.md`](hardware_contract.md) - physical interface and signal contract
5. [`dual_core_partitioning.md`](dual_core_partitioning.md) - realtime and service-role ownership
6. [`bringup.md`](bringup.md) - physical bring-up and operating procedure

The central architectural boundary is:

> **PIO/DMA and bounded on-chip state own current-cycle V30 timing; Arm software, USB, storage, and host tools operate outside that active-cycle path.**

The current machine model is defined by [`adr/0007-adopt-host-constructed-v30-machine-model.md`](adr/0007-adopt-host-constructed-v30-machine-model.md).

## Canonical architecture and interfaces

| Document | Role |
|---|---|
| [`architecture.md`](architecture.md) | Overall host-constructed physical V30 machine architecture |
| [`memory_architecture.md`](memory_architecture.md) | Canonical memory terminology, minimum V30 Memory Map, SRAM/PSRAM/Flash policy |
| [`host_protocol.md`](host_protocol.md) | USB HID command/response and CDC observation contract |
| [`hardware_contract.md`](hardware_contract.md) | Canonical Raspberry Pi 40-pin physical interface |
| [`pin_mapping.md`](pin_mapping.md) | GPIO, header, and V30 signal mapping |
| [`dual_core_partitioning.md`](dual_core_partitioning.md) | Realtime-control and asynchronous-service ownership |
| [`adr/0007-adopt-host-constructed-v30-machine-model.md`](adr/0007-adopt-host-constructed-v30-machine-model.md) | Current architecture decision |
| [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) | Current deterministic memory/READY constraint |

Canonical architecture uses three actors:

```text
Host
  |
  | USB HID + CDC
  v
RP2350 Machine Platform
  |
  v
Physical NEC V30
```

The project defines the Host wire protocol, not a mandatory Python/C/Rust/Web/AI SDK stack.

## Validated mechanisms and optional services

Several documents describe mechanisms that were validated during earlier architecture stages. They remain useful engineering evidence and may be reused by optional workloads, but they are not requirements of the minimum machine architecture.

| Document | Current role |
|---|---|
| [`companion_service_abi.md`](companion_service_abi.md) | Validated 64-byte Host Bridge record and V30 mailbox/service mechanism |
| [`ai_bridge_architecture.md`](ai_bridge_architecture.md) | Provider-neutral host/AI bridge experiment architecture |
| [`archive/completed-plans/ai_bridge_implementation_plan.md`](archive/completed-plans/ai_bridge_implementation_plan.md) | Archived bridge implementation and validation plan |
| [`archive/completed-plans/pc1c0c1_arbitrary_sram_rom_architecture.md`](archive/completed-plans/pc1c0c1_arbitrary_sram_rom_architecture.md) | Archived address-qualified internal-SRAM response research |

The 64-byte record framing in the Companion Service ABI remains a useful Host Protocol foundation. BIOS `INT 60h`, PIT heartbeat, V30 persistent-runtime, and AI-specific flows are optional validated mechanisms rather than core requirements.

AI is simply one possible host-side client of the same HID/CDC machine interface used by conventional tools.

## Memory terminology

Canonical documents distinguish physical resources from V30-visible semantics:

- **RP2350 Internal SRAM** - deterministic on-chip working state;
- **External NOR Flash** - persistent firmware and filesystem/assets;
- **External PSRAM** - target-machine bulk volatile backing/workspace;
- **SD Card** - optional removable bulk storage;
- **V30 Memory Map** - CPU-visible address-space semantics;
- **Backing Resource** - implementation used to materialize a mapped region.

Do not treat the V30 Memory Map as synonymous with a particular physical RAM device.

See [`memory_architecture.md`](memory_architecture.md).

## Hardware

- [`hardware.md`](hardware.md) - physical platform and electrical notes
- [`hardware_contract.md`](hardware_contract.md) - canonical current hardware contract
- [`pin_mapping.md`](pin_mapping.md) - current signal mapping
- [`pi86_hat_design_review.md`](pi86_hat_design_review.md) - engineering review of the original Pi86 HAT
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md) - 40-pin physical ABI decision
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - deterministic memory/READY policy

The existing Pi86 HAT remains the working hardware baseline. Older replacement-board concepts, including [`hardware/v3_companion_board_architecture.md`](hardware/v3_companion_board_architecture.md), are retained as historical design records rather than the current project direction.

## Development and engineering method

- [`development/build_and_toolchain.md`](development/build_and_toolchain.md) - build environment
- [`toolchain.md`](toolchain.md) - toolchain reference
- [`../ENGINEERING_PLAYBOOK.md`](../ENGINEERING_PLAYBOOK.md) - engineering workflow
- [`archive/legacy-methodology/engineering_playbook/README.md`](archive/legacy-methodology/engineering_playbook/README.md) - archived project-local methodology seed
- [`bringup.md`](bringup.md) - physical build/flash/bring-up entrypoint
- [`bringup/recovery.md`](bringup/recovery.md) - flashing, USB, evidence, and rollback recovery
- [`../tools/docs/README.md`](../tools/docs/README.md) - documentation checks

## Optional workloads and compatibility

BIOS, PIC/PIT/PPI services, DOS, ELKS, PC memory maps, disk-image boot paths, display, keyboard, and other PC-class behavior are optional workloads or compatibility profiles.

Relevant documents include:

- [`archive/completed-plans/native_bios_architecture.md`](archive/completed-plans/native_bios_architecture.md)
- [`native_bios_diagnostic_console.md`](native_bios_diagnostic_console.md)
- [`archive/completed-plans/pc1c1_native_bios_platform.md`](archive/completed-plans/pc1c1_native_bios_platform.md)
- [`archive/completed-plans/minimal_pc_compatibility_matrix.md`](archive/completed-plans/minimal_pc_compatibility_matrix.md)
- [`elks_v30_fd1440_bringup.md`](elks_v30_fd1440_bringup.md)

These documents are retained because they describe useful native workloads, compatibility mechanisms, and development history. They do not define the minimum architecture.

## Validation and history

[`validation/`](validation/) contains accepted physical validation records. [`bringup/gate_history.md`](bringup/gate_history.md), [`retrospectives/`](retrospectives/), [`releases/`](releases/), and [`story/`](story/) preserve development history and measured results. Superseded plans and early bring-up notes are indexed by [`archive/README.md`](archive/README.md).

Historical documents remain historical: architecture cleanup must not rewrite old measurements, clocks, gate names, captured outputs, or acceptance claims. Add a superseding ADR or canonical document when interpretation changes.

## Architecture decisions

[`adr/`](adr/) records architecture decisions, their context, and consequences.

Important current decisions include:

- [`adr/0007-adopt-host-constructed-v30-machine-model.md`](adr/0007-adopt-host-constructed-v30-machine-model.md) - current host-constructed machine model;
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - deterministic memory constraint;
- [`adr/0006-retain-current-pi86-hat-as-hardware-baseline.md`](adr/0006-retain-current-pi86-hat-as-hardware-baseline.md) - current HAT baseline.

Earlier ADRs remain part of the decision history. When a later ADR supersedes an earlier roadmap assumption, preserve the older record rather than silently rewriting it.

## Documentation maintenance

Keep the public README compact. Put stable system structure in canonical architecture/contracts, changing implementation work in plans and issues, and exact measured behavior in validation/history documents. Prefer links to canonical interfaces instead of duplicating protocol tables, memory policy, pin maps, or timing rules across multiple files.
