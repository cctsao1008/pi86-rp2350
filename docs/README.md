# pi86-rp2350 Documentation

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel
> 8086 and NEC V30 processors.**

Documentation is organized by authority rather than by the order in which the project evolved.

## Start here

1. [`architecture/system_architecture.md`](architecture/system_architecture.md) — system identity, roles, and boundaries
2. [`architecture/host_runtime.md`](architecture/host_runtime.md) — Host runtime and ownership model
3. [`architecture/memory.md`](architecture/memory.md) — processor-visible memory and storage ownership
4. [`architecture/repository_structure.md`](architecture/repository_structure.md) — source-tree ownership model
5. [`reference/processor_memory_map.md`](reference/processor_memory_map.md) — canonical 8086/V30 physical address map
6. [`reference/processor_io_interrupt_map.md`](reference/processor_io_interrupt_map.md) — processor I/O ports and interrupt vectors
7. [`reference/host_protocol.md`](reference/host_protocol.md) — Host operations and transports
8. [`reference/companion_service_abi.md`](reference/companion_service_abi.md) — records and processor mailbox
9. [`reference/hardware.md`](reference/hardware.md) — board resources and electrical ownership
10. [`bringup/README.md`](bringup/README.md) — physical bring-up and acceptance
11. [`development/build_and_toolchain.md`](development/build_and_toolchain.md) — build procedure
12. [`development/host_runtime_shell.md`](development/host_runtime_shell.md) — RP86 Host shell
13. [`development/workload_deployment_vs_regression.md`](development/workload_deployment_vs_regression.md) — persistent deployment vs finite validation
14. [`development/windows_physical_validation.md`](development/windows_physical_validation.md) — live hardware workflow
15. [`bringup/recovery.md`](bringup/recovery.md) — recovery
16. [`development/codex_physical_development_loop.md`](development/codex_physical_development_loop.md) — closed physical development loop
17. [`../processor/README.md`](../processor/README.md) — native processor runtime and workloads
18. [`reference/README.md`](reference/README.md) — external specification and implementation references

```text
Host       = Runtime Controller
RP2350     = Companion Resource and Bus Controller
8086 / V30 = Bare-Metal Remote Physical Processor
```

## Documentation authority

```text
docs/architecture/   durable current system architecture
docs/reference/      exact stable technical reference and ABI/maps
docs/development/    build/debug/developer workflows
docs/validation/     acceptance methods and accepted evidence
docs/bringup/        physical bring-up and recovery
docs/adr/            architectural decisions and rationale
docs/story/          public narrative only; not normative architecture
```

## Architectural decisions

- [`ADR 0001`](adr/0001-use-rpi-physical-pin-as-hardware-abi.md) — physical header ABI
- [`ADR 0003`](adr/0003-define-physical-timing-boundary.md) — physical timing boundary
- [`ADR 0006`](adr/0006-retain-current-pi86-hat-as-hardware-baseline.md) — Pi86 HAT baseline
- [`ADR 0008`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — runtime identity
- [`ADR 0009`](adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md) — processor scope
- [`ADR 0010`](adr/0010-adopt-free-running-and-clock-stepped-execution.md) — execution clock modes

## Current physical evidence

- [`canonical_runtime_integration_1mhz_validation.md`](validation/canonical_runtime_integration_1mhz_validation.md)
- [`internal_sram_workload_staging_1mhz_validation.md`](validation/internal_sram_workload_staging_1mhz_validation.md)
- [`native_calculator_1mhz_validation.md`](validation/native_calculator_1mhz_validation.md)
- [`native_fast_invsqrt_intel_8086_validation.md`](validation/native_fast_invsqrt_intel_8086_validation.md)
- [`host_loaded_internal_sram_calculator_1mhz_validation.md`](validation/host_loaded_internal_sram_calculator_1mhz_validation.md)
- [`intel_8086_interactive_heartbeat_1mhz_observation.md`](validation/intel_8086_interactive_heartbeat_1mhz_observation.md)
- [`clock_stepped_internal_sram_general_execution_validation.md`](validation/clock_stepped_internal_sram_general_execution_validation.md)
- [`execution_clock_mode_transition_validation.md`](validation/execution_clock_mode_transition_validation.md)
- [`internal_sram_shared_mailbox_validation.md`](validation/internal_sram_shared_mailbox_validation.md)

[`story/`](story/) contains the four public articles. They preserve project narrative, not competing architecture specifications.

## Placement rule

```text
stable architecture             -> architecture/
stable interface / ABI / map   -> reference/
development workflow            -> development/
accepted physical measurement   -> validation/
physical bring-up / recovery    -> bringup/
one architectural decision      -> adr/
public narrative                -> story/
superseded material             -> Git history
```

The repository intentionally has no documentation archive. Git is the archive.
