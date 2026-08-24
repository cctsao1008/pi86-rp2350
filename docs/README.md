# pi86-rp2350 Documentation

This directory contains the detailed architecture, interface contracts, engineering notes, implementation plans, and historical validation records for `pi86-rp2350`.

The top-level [`README.md`](../README.md) is the public project overview. Documents here carry the implementation and engineering detail that does not belong on the repository front page.

## Start here

1. [`architecture.md`](architecture.md) - canonical RP2350/V30 system architecture
2. [`hardware_contract.md`](hardware_contract.md) - physical interface and signal contract
3. [`dual_core_partitioning.md`](dual_core_partitioning.md) - realtime and service-role ownership
4. [`companion_service_abi.md`](companion_service_abi.md) - host/V30 companion-service ABI
5. [`ai_bridge_architecture.md`](ai_bridge_architecture.md) - provider-neutral host bridge
6. [`bringup.md`](bringup.md) - physical bring-up and operating procedure

## Architecture and interfaces

| Document | Role |
|---|---|
| [`architecture.md`](architecture.md) | Overall physical V30 + RP2350 companion-chip architecture |
| [`hardware_contract.md`](hardware_contract.md) | Canonical Raspberry Pi 40-pin physical interface |
| [`pin_mapping.md`](pin_mapping.md) | GPIO, header, and V30 signal mapping |
| [`dual_core_partitioning.md`](dual_core_partitioning.md) | Realtime-control and asynchronous-service roles |
| [`companion_service_abi.md`](companion_service_abi.md) | Host records and V30-visible mailbox/service ABI |
| [`pc1c0c1_arbitrary_sram_rom_architecture.md`](pc1c0c1_arbitrary_sram_rom_architecture.md) | Address-qualified internal-SRAM response research |
| [`native_bios_architecture.md`](native_bios_architecture.md) | Native V30 BIOS workload architecture |
| [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) | Optional PC-class compatibility profile |

The central architectural boundary is simple: **PIO/DMA and bounded on-chip state own current-cycle V30 timing; Arm software, host tools, storage, and AI operate around that realtime path.**

## Host, observation, and AI

| Document | Role |
|---|---|
| [`ai_bridge_architecture.md`](ai_bridge_architecture.md) | Provider-neutral host/V30 translation boundary |
| [`companion_service_abi.md`](companion_service_abi.md) | Machine-readable host and companion-service records |
| [`ai_bridge_implementation_plan.md`](ai_bridge_implementation_plan.md) | Historical/current bridge implementation work |
| [`adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md`](adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md) | Mailbox sequencer decision |
| [`adr/0005-adopt-host-bridge-and-companion-service-terminology.md`](adr/0005-adopt-host-bridge-and-companion-service-terminology.md) | Provider-neutral terminology decision |

AI is a host-side client of the same observation, control, and experiment interfaces available to conventional tools. The V30-visible side remains an ordinary machine interface built from memory, I/O, interrupts, and mailbox state.

## Hardware

- [`hardware.md`](hardware.md) - physical platform and electrical notes
- [`hardware_contract.md`](hardware_contract.md) - canonical current hardware contract
- [`pin_mapping.md`](pin_mapping.md) - current signal mapping
- [`pi86_hat_design_review.md`](pi86_hat_design_review.md) - engineering review of the original Pi86 HAT
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md) - 40-pin physical ABI decision
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - deterministic memory/READY policy

The existing Pi86 HAT is the working hardware baseline. Older replacement-board concepts, including [`hardware/v3_companion_board_architecture.md`](hardware/v3_companion_board_architecture.md), are retained as historical design records rather than the current project direction.

## Development and engineering method

- [`development/build_and_toolchain.md`](development/build_and_toolchain.md) - build environment
- [`toolchain.md`](toolchain.md) - toolchain reference
- [`../ENGINEERING_PLAYBOOK.md`](../ENGINEERING_PLAYBOOK.md) - engineering workflow
- [`engineering_playbook/README.md`](engineering_playbook/README.md) - playbook index
- [`bringup.md`](bringup.md) - physical build/flash/bring-up entrypoint
- [`bringup/recovery.md`](bringup/recovery.md) - flashing, USB, evidence, and rollback recovery
- [`../tools/docs/README.md`](../tools/docs/README.md) - documentation checks

## Workloads and compatibility

BIOS, PIC/PIT services, diagnostic ROMs, DOS, ELKS, storage, display, keyboard, and other PC-class behavior are treated as workloads or optional compatibility profiles around the same physical V30/RP2350 architecture.

Relevant documents include:

- [`native_bios_architecture.md`](native_bios_architecture.md)
- [`native_bios_diagnostic_console.md`](native_bios_diagnostic_console.md)
- [`pc1c1_native_bios_platform.md`](pc1c1_native_bios_platform.md)
- [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md)
- [`elks_v30_fd1440_bringup.md`](elks_v30_fd1440_bringup.md)

## Validation and history

[`validation/`](validation/) contains accepted physical validation records. [`bringup/gate_history.md`](bringup/gate_history.md), [`retrospectives/`](retrospectives/), [`releases/`](releases/), and [`story/`](story/) preserve development history and measured results.

Historical documents should remain historical: later architecture cleanup should not rewrite old measurements, clocks, gate names, or captured outputs. Add a dated clarification or superseding document when interpretation changes.

## Architecture decisions

[`adr/`](adr/) records architecture decisions, their context, and consequences. When a decision changes materially, prefer a new superseding ADR rather than silently rewriting the original decision.

## Documentation maintenance

Keep the public README compact. Put stable system structure in canonical architecture/contracts, changing implementation work in plans and issues, and exact measured behavior in validation/history documents. Prefer links to canonical interfaces instead of duplicating pin maps, ABI tables, or timing rules across multiple files.
