# pi86-rp2350 Documentation

This directory contains the design contracts, implementation plans, engineering methods, and physical evidence for `pi86-rp2350`.

The documentation is organized by authority and purpose. Architecture documents describe the intended system. Validation records describe what a specific physical run proved. A target is not considered implemented merely because it appears in a design document.

## Start here

1. [`../README.md`](../README.md) - project purpose, architecture, and long-term goals
2. [`project_overview.md`](project_overview.md) - compact system scope and research model
3. [`architecture.md`](architecture.md) - companion-chip timing and ownership architecture
4. [`hardware_contract.md`](hardware_contract.md) - canonical physical interface
5. [`ai_bridge_architecture.md`](ai_bridge_architecture.md) - provider-neutral host bridge

## Architecture and contracts

| Document | Role |
|---|---|
| [`architecture.md`](architecture.md) | RP2350/V30 data, control, and service planes |
| [`companion_service_abi.md`](companion_service_abi.md) | canonical 64-byte host record and V30 mailbox ABI |
| [`hardware_contract.md`](hardware_contract.md) | canonical physical header and signal contract |
| [`pin_mapping.md`](pin_mapping.md) | GPIO, header, and V30 signal mapping |
| [`dual_core_partitioning.md`](dual_core_partitioning.md) | realtime/service roles and queue ownership |
| [`native_bios_architecture.md`](native_bios_architecture.md) | native V30 BIOS design |
| [`pc1c0c1_arbitrary_sram_rom_architecture.md`](pc1c0c1_arbitrary_sram_rom_architecture.md) | arbitrary internal-SRAM ROM response research |
| [`pc1c1_native_bios_platform.md`](pc1c1_native_bios_platform.md) | native BIOS platform contracts |
| [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md) | dependency-driven PC compatibility model |

## Host and AI bridge

| Document | Role |
|---|---|
| [`ai_bridge_architecture.md`](ai_bridge_architecture.md) | stable host/V30 translation boundary |
| [`ai_bridge_implementation_plan.md`](ai_bridge_implementation_plan.md) | staged gates and accepted implementations |
| [`development/windows_physical_validation.md`](development/windows_physical_validation.md) | Windows HID/CDC validation workflow |
| [`adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md`](adr/0004-use-parallel-pio-sequencers-for-ai-mailbox.md) | mailbox sequencer decision |
| [`adr/0005-adopt-host-bridge-and-companion-service-terminology.md`](adr/0005-adopt-host-bridge-and-companion-service-terminology.md) | provider-neutral terminology decision |

The V30-visible interface is a companion service, not an AI abstraction. Codex was the first validated host adapter; conventional software, ChatGPT, an OpenAI API client, or another tool may use the same provider-neutral bridge.

## Hardware

- [`hardware.md`](hardware.md) - present physical platform and electrical notes
- [`pi86_hat_design_review.md`](pi86_hat_design_review.md) - original HAT review and future board direction
- [`adr/0001-use-rpi-physical-pin-as-hardware-abi.md`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md) - 40-pin physical ABI decision
- [`adr/0003-require-ready-or-deterministic-hits-for-general-memory.md`](adr/0003-require-ready-or-deterministic-hits-for-general-memory.md) - READY and deterministic memory policy

## Development and engineering method

- [`development/build_and_toolchain.md`](development/build_and_toolchain.md) - build environment
- [`toolchain.md`](toolchain.md) - toolchain reference
- [`ENGINEERING_PLAYBOOK.md`](../ENGINEERING_PLAYBOOK.md) - top-level engineering workflow
- [`engineering_playbook/README.md`](engineering_playbook/README.md) - playbook index
- [`engineering_playbook/engineering_truth_hierarchy.md`](engineering_playbook/engineering_truth_hierarchy.md) - evidence authority
- [`engineering_playbook/test_acceptance_criteria.md`](engineering_playbook/test_acceptance_criteria.md) - acceptance rules
- [`engineering_playbook/diagnostic_design.md`](engineering_playbook/diagnostic_design.md) - diagnostic design method
- [`bringup.md`](bringup.md) - current physical build/flash/validation entrypoint
- [`bringup/recovery.md`](bringup/recovery.md) - flashing, USB, evidence, and rollback recovery

## Plans and compatibility work

Planning documents describe intended work and may contain historical gate names:

- [`pc1c_rom_execution_plan.md`](pc1c_rom_execution_plan.md)
- [`ai_bridge_implementation_plan.md`](ai_bridge_implementation_plan.md)
- [`minimal_pc_compatibility_matrix.md`](minimal_pc_compatibility_matrix.md)
- [`gate12_scope.md`](gate12_scope.md)

Use Git history and validation records to determine whether a planned gate has been accepted.

## Validation evidence

[`validation/`](validation/) contains physical acceptance records. These documents preserve the exact tested target, configured clock, firmware identity, observed output, scope, and limitations.

Important evidence families include:
- [`bringup/gate_history.md`](bringup/gate_history.md) - retained Gate 0–12 development record;

- PC1-B PIO-direct response and frequency characterization;
- PC1-C address capture, ROM, RAM, and Native BIOS execution;
- dual-core service isolation and trace backpressure;
- AI-B0/B1/B2/B3 mailbox, HID/CDC, and Codex-adapter validation;
- PIC, multi-IRQ, and PIT interrupt validation.

Validation documents are engineering history. Do not rewrite their measured values to match a later architecture or naming preference. Add a clearly dated note if later interpretation changes.

## Architecture decisions

[`adr/`](adr/) records decisions, context, consequences, and superseded assumptions. ADRs may be clarified, but accepted decisions should not be silently rewritten; supersede them with a new ADR when the decision itself changes.

## Retrospectives and releases

- [`retrospectives/`](retrospectives/) - lessons from completed work
- [`releases/`](releases/) - milestone summaries

These are historical records, not the canonical current architecture.

## Documentation maintenance rules

1. Keep the main README and architecture documents stable and goal-oriented.
2. Put changing implementation order in plan documents.
3. Put exact physical output and limitations in validation records.
4. Keep provider-specific AI behavior above the Host Bridge boundary.
5. Distinguish fixed, address-qualified, bounded, cached, and general response engines.
6. Prefer links to canonical contracts instead of duplicating pin maps or ABI tables.
7. Never convert a target capability into a validated claim without physical evidence.

