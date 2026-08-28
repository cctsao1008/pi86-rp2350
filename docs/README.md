# pi86-rp2350 Documentation

The current project definition is:

> **pi86-rp2350 is a host-managed bare-metal processor runtime for real Intel 8086 and NEC V30 processors.**

Historical experiments remain available as evidence, but they do not define the
current architecture.

## Start here

The active documentation set is intentionally small:

1. [`architecture.md`](architecture.md) — canonical identity, roles, runtime, and boundaries
2. [`host_runtime_architecture.md`](host_runtime_architecture.md) — detailed runtime, resources, permissions, and failure model
3. [`host_runtime_shell.md`](host_runtime_shell.md) — RP86 Host shell and command framework
4. [`memory_architecture.md`](memory_architecture.md) — Internal SRAM, PSRAM, NOR, SD, FAT, and shared ownership
5. [`host_protocol.md`](host_protocol.md) — language-independent Host operations and transport semantics
6. [`companion_service_abi.md`](companion_service_abi.md) — 64-byte Host/Core record and processor mailbox ABI
7. [`hardware.md`](hardware.md) — board resources, locked signal mapping, and electrical ownership
8. [`bringup.md`](bringup.md) — current physical bring-up and acceptance workflow
9. [`development/build_and_toolchain.md`](development/build_and_toolchain.md) — canonical build instructions
10. [`development/windows_physical_validation.md`](development/windows_physical_validation.md) — Windows physical validation workflow
11. [`bringup/recovery.md`](bringup/recovery.md) — recovery procedures
12. [`development/codex_physical_development_loop.md`](development/codex_physical_development_loop.md) — Codex-to-physical-processor implementation, deployment, and evidence loop

The three roles are:

```text
Host       = Runtime Controller
RP2350     = Companion Resource and Bus Controller
8086 / V30 = Bare-Metal Remote Physical Processor
```

## Document authority

| Topic | Canonical document |
|---|---|
| Project identity and boundary | [`architecture.md`](architecture.md) |
| Runtime states, services, and ownership | [`host_runtime_architecture.md`](host_runtime_architecture.md) |
| Operator commands | [`host_runtime_shell.md`](host_runtime_shell.md) |
| Memory and persistent storage | [`memory_architecture.md`](memory_architecture.md) |
| Host operations and transport | [`host_protocol.md`](host_protocol.md) |
| Record and mailbox ABI | [`companion_service_abi.md`](companion_service_abi.md) |
| Board, pins, and electrical contract | [`hardware.md`](hardware.md) |
| Build and physical validation | [`development/`](development/) and [`bringup.md`](bringup.md) |
| Codex-assisted physical development loop | [`development/codex_physical_development_loop.md`](development/codex_physical_development_loop.md) |

Avoid redefining the project in another document. Link to the appropriate
canonical contract instead.

## Decisions

The files under [`adr/`](adr/) preserve architectural decisions individually.
They remain separate because each records one decision, its context, and its
consequences. Current defining decisions include:

- [`ADR 0008`](adr/0008-adopt-host-managed-bare-metal-processor-runtime.md) — Host-managed bare-metal runtime
- [`ADR 0009`](adr/0009-extend-runtime-to-intel-8086-and-nec-v30.md) — Intel 8086 and NEC V30 scope
- [`ADR 0010`](adr/0010-adopt-continuous-and-paced-clock-engines.md) — CONTINUOUS and PACED execution engines

## Validation evidence

[`validation/`](validation/) contains accepted physical evidence. Validation
files remain separate so their original firmware identity, clock, output,
limitations, and result cannot be blurred by later architecture changes.

Useful current evidence includes:

- [`canonical_runtime_integration_1mhz_validation.md`](validation/canonical_runtime_integration_1mhz_validation.md)
- [`internal_sram_workload_staging_1mhz_validation.md`](validation/internal_sram_workload_staging_1mhz_validation.md)
- [`native_calculator_1mhz_validation.md`](validation/native_calculator_1mhz_validation.md)
- [`host_loaded_internal_sram_calculator_1mhz_validation.md`](validation/host_loaded_internal_sram_calculator_1mhz_validation.md)
- [`intel_8086_interactive_heartbeat_1mhz_observation.md`](validation/intel_8086_interactive_heartbeat_1mhz_observation.md)
- [`paced_internal_sram_general_execution_validation.md`](validation/paced_internal_sram_general_execution_validation.md)

## Story and retrospective

- [`story/`](story/) preserves the four public development articles.
- [`retrospectives/`](retrospectives/) records engineering lessons learned.

These are narrative records, not competing architecture specifications.

## Archive

[`archive/`](archive/) holds superseded architecture, completed plans, early
bring-up history, former standalone contracts, and historical references.
Archive status does not mean the work was wrong; it means the document no
longer defines the active project.

In particular:

- old project overviews and AI-bridge-first architecture are under
  `archive/superseded-architecture/`;
- former standalone hardware and pin contracts are under
  `archive/hardware-concepts/`, after consolidation into [`hardware.md`](hardware.md);
- the original Gate 0–12 sequence is under `archive/early-bringup/`;
- former toolchain and milestone documents are retained under their matching
  archive categories.

## Documentation rule

Use this placement rule:

```text
stable architecture or interface  -> canonical contract
current implementation work       -> GitHub issue
accepted physical measurement     -> validation/
one architectural decision        -> adr/
public narrative                  -> story/
superseded direction or history   -> archive/
```

The goal is not the smallest possible file count. The goal is one authoritative
place for each current fact and a short, predictable reading path.
