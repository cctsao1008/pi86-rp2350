# Documentation Archive

## Purpose

This directory preserves documents that were important to the development of
`pi86-rp2350` but no longer describe the canonical current architecture or
active implementation boundary.

Archive status does not mean that the recorded experiment, decision process, or
measured evidence was invalid. It means that a later architecture, accepted
validation record, or project direction has replaced the document's active
role.

Canonical current documents remain indexed in [`../README.md`](../README.md).
Exact physical acceptance records remain under [`../validation/`](../validation/).

## Maintenance rules

1. Do not rewrite historical measurements to match the current architecture.
2. Prefer Git history and a replacement link over deleting engineering context.
3. Keep validation evidence, current ADRs, and project stories outside the
   archive. Superseded ADRs and milestone summaries may be archived when a
   later contract or validation record replaces their active role.
4. Move a document here only when its active role has been replaced or it
   duplicates a retained canonical record.
5. New plans and contracts must not use archived files as current authority.

## Archived sets

### Early bring-up and characterization

| Archived document | Former role | Current authority |
|---|---|---|
| [`early-bringup/pc1a_rev0_review.md`](early-bringup/pc1a_rev0_review.md) | PC1-A polling-path review | [`../architecture.md`](../architecture.md) and validation evidence |
| [`early-bringup/pc1a_to_pc1b_architecture_decision.md`](early-bringup/pc1a_to_pc1b_architecture_decision.md) | Transition from M33 polling to PIO timing | [`../architecture.md`](../architecture.md) |
| [`early-bringup/performance_characterization_1.md`](early-bringup/performance_characterization_1.md) | PC1-A/PC1-B characterization plan and record | [`../validation/pc1b_pio_direct_frequency_sweep.md`](../validation/pc1b_pio_direct_frequency_sweep.md) |
| [`early-bringup/pc1b_pio_direct_frequency_sweep_20260817.md`](early-bringup/pc1b_pio_direct_frequency_sweep_20260817.md) | Dated duplicate sweep summary | [`../validation/pc1b_pio_direct_frequency_sweep.md`](../validation/pc1b_pio_direct_frequency_sweep.md) |
| [`early-bringup/bringup_gate11.md`](early-bringup/bringup_gate11.md) | Gate 11 implementation/acceptance note | [`../validation/gate11_multi_irq_priority_validation.md`](../validation/gate11_multi_irq_priority_validation.md) |
| [`early-bringup/bringup_gate12.md`](early-bringup/bringup_gate12.md) | Gate 12 implementation/acceptance note | [`../validation/gate12_pit_irq0_validation.md`](../validation/gate12_pit_irq0_validation.md) |
| [`early-bringup/gate12_scope.md`](early-bringup/gate12_scope.md) | Completed Gate 12 scope decision | [`early-bringup/gate_history.md`](early-bringup/gate_history.md) |
| [`early-bringup/gate_history.md`](early-bringup/gate_history.md) | Original Gate 0–12 development sequence | [`../bringup.md`](../bringup.md) and individual validation records |

### Completed or superseded plans

| Archived document | Former role | Current authority |
|---|---|---|
| [`completed-plans/pc1c_rom_execution_plan.md`](completed-plans/pc1c_rom_execution_plan.md) | PC1-C implementation plan | PC1-C records under [`../validation/`](../validation/) |
| [`completed-plans/pc1c0c1_arbitrary_sram_rom_architecture.md`](completed-plans/pc1c0c1_arbitrary_sram_rom_architecture.md) | Bounded selector/RAM research architecture | [`../architecture.md`](../architecture.md) and [`../memory_architecture.md`](../memory_architecture.md) |
| [`completed-plans/pc1c1_native_bios_platform.md`](completed-plans/pc1c1_native_bios_platform.md) | Native BIOS platform plan | [`../architecture.md`](../architecture.md) |
| [`completed-plans/native_bios_architecture.md`](completed-plans/native_bios_architecture.md) | Early Native BIOS direction | BIOS as an optional workload |
| [`completed-plans/minimal_pc_compatibility_matrix.md`](completed-plans/minimal_pc_compatibility_matrix.md) | PC-first compatibility roadmap | PC compatibility as an optional experiment |
| [`completed-plans/ai_bridge_implementation_plan.md`](completed-plans/ai_bridge_implementation_plan.md) | AI-B0 through AI-B3 gate ledger | [`superseded-architecture/host_bridge_and_ai_client_architecture.md`](superseded-architecture/host_bridge_and_ai_client_architecture.md), [`../companion_service_abi.md`](../companion_service_abi.md), and accepted validation records |

### Former project architectures

| Archived document | Former role | Current authority |
|---|---|---|
| [`superseded-architecture/adr-0002-adopt-v30-companion-chip-architecture.md`](superseded-architecture/adr-0002-adopt-v30-companion-chip-architecture.md) | Companion-chip and PC-oriented roadmap | [`../architecture.md`](../architecture.md) and ADR 0008 |
| [`superseded-architecture/adr-0005-adopt-host-bridge-and-companion-service-terminology.md`](superseded-architecture/adr-0005-adopt-host-bridge-and-companion-service-terminology.md) | Former project-level terminology | [`../architecture.md`](../architecture.md) and ADR 0008 |
| [`superseded-architecture/adr-0007-adopt-host-constructed-v30-machine-model.md`](superseded-architecture/adr-0007-adopt-host-constructed-v30-machine-model.md) | Host-constructed programmable-machine model | [`../architecture.md`](../architecture.md) and ADR 0008 |
| [`superseded-architecture/project_overview.md`](superseded-architecture/project_overview.md) | Standalone project overview | Root README and [`../architecture.md`](../architecture.md) |
| [`superseded-architecture/host_bridge_and_ai_client_architecture.md`](superseded-architecture/host_bridge_and_ai_client_architecture.md) | AI-bridge-first architecture | [`../host_protocol.md`](../host_protocol.md) and [`../host_runtime_architecture.md`](../host_runtime_architecture.md) |

### Optional compatibility and hardware concepts

| Archived document | Former role | Current authority |
|---|---|---|
| [`compatibility/elks_v30_fd1440_bringup.md`](compatibility/elks_v30_fd1440_bringup.md) | ELKS floppy-image build and boot direction | ELKS is an optional workload |
| [`compatibility/native_bios_diagnostic_console.md`](compatibility/native_bios_diagnostic_console.md) | Native BIOS diagnostic console contract | Host runtime stdio and Companion Service ABI |
| [`hardware-concepts/v3_companion_board_architecture.md`](hardware-concepts/v3_companion_board_architecture.md) | Replacement companion-board concept | Current Pi86 HAT hardware contract |
| [`hardware-concepts/hardware_contract.md`](hardware-concepts/hardware_contract.md) | Standalone electrical/ownership contract | Consolidated [`../hardware.md`](../hardware.md) |
| [`hardware-concepts/pin_mapping.md`](hardware-concepts/pin_mapping.md) | Standalone signal mapping | Consolidated [`../hardware.md`](../hardware.md) |
| [`hardware-concepts/pi86_hat_design_review.md`](hardware-concepts/pi86_hat_design_review.md) | Original HAT design review | Consolidated [`../hardware.md`](../hardware.md) and historical context |

### Implementation, development, releases, and references

| Archived document | Former role | Current authority |
|---|---|---|
| [`implementation-history/dual_core_partitioning.md`](implementation-history/dual_core_partitioning.md) | Standalone dual-core partition contract | [`../host_runtime_architecture.md`](../host_runtime_architecture.md) |
| [`development-history/toolchain.md`](development-history/toolchain.md) | Earlier detailed toolchain guide | [`../development/build_and_toolchain.md`](../development/build_and_toolchain.md) |
| [`releases/gate10_milestone.md`](releases/gate10_milestone.md) | Gate 10 milestone summary | [`../validation/gate10_8259a_validation.md`](../validation/gate10_8259a_validation.md) |
| [`releases/gate11_milestone.md`](releases/gate11_milestone.md) | Gate 11 milestone summary | [`../validation/gate11_multi_irq_priority_validation.md`](../validation/gate11_multi_irq_priority_validation.md) |
| [`reference/8259a_reference.md`](reference/8259a_reference.md) | PC/PIC-focused reference | Optional compatibility background |

### Legacy methodology seed

[`legacy-methodology/engineering_playbook/`](legacy-methodology/engineering_playbook/)
is the project-local seed from which the standalone
[`technical-management-framework`](https://github.com/cctsao1008/technical-management-framework)
was derived. The external repository is the active authority for reusable
cross-project methodology.
