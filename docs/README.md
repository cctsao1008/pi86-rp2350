# pi86-rp2350 Documentation

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel
> 8086 and NEC V30 processors.**

## Start here

1. [`architecture.md`](architecture.md) — identity, roles, and boundaries
2. [`host_runtime_architecture.md`](host_runtime_architecture.md) — runtime and ownership model
3. [`host_runtime_shell.md`](host_runtime_shell.md) — RP86 Host shell
4. [`memory_architecture.md`](memory_architecture.md) — SRAM, PSRAM, flash, SD, and sharing
5. [`host_protocol.md`](host_protocol.md) — Host operations and transports
6. [`companion_service_abi.md`](companion_service_abi.md) — records and processor mailbox
7. [`hardware.md`](hardware.md) — board resources and electrical ownership
8. [`bringup.md`](bringup.md) — physical bring-up and acceptance
9. [`development/build_and_toolchain.md`](development/build_and_toolchain.md) — build procedure
10. [`development/windows_physical_validation.md`](development/windows_physical_validation.md) — live hardware workflow
11. [`bringup/recovery.md`](bringup/recovery.md) — recovery
12. [`development/codex_physical_development_loop.md`](development/codex_physical_development_loop.md) — closed physical development loop

```text
Host       = Runtime Controller
RP2350     = Companion Resource and Bus Controller
8086 / V30 = Bare-Metal Remote Physical Processor
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
- [`host_loaded_internal_sram_calculator_1mhz_validation.md`](validation/host_loaded_internal_sram_calculator_1mhz_validation.md)
- [`intel_8086_interactive_heartbeat_1mhz_observation.md`](validation/intel_8086_interactive_heartbeat_1mhz_observation.md)
- [`clock_stepped_internal_sram_general_execution_validation.md`](validation/clock_stepped_internal_sram_general_execution_validation.md)
- [`execution_clock_mode_transition_validation.md`](validation/execution_clock_mode_transition_validation.md)

[`story/`](story/) contains the four public articles. They are narrative, not
competing architecture specifications.

## Placement rule

```text
stable architecture or interface  -> canonical document
current implementation work       -> GitHub issue
accepted physical measurement     -> validation/
one architectural decision        -> adr/
public narrative                  -> story/
superseded material                -> Git history
```

The repository intentionally has no documentation archive. Git is the archive.
