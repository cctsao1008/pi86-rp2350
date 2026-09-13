# Repository Architecture

This document defines the canonical source-tree ownership model for `pi86-rp2350`.

The repository is organized first by **execution owner**, then by implementation responsibility. Directory placement must answer where code executes and who owns its contract without relying on project history.

## Top-level ownership

```text
pi86-rp2350/
├── host/          # production PC-side software
├── firmware/      # production RP2350 firmware
├── processor/     # native Intel 8086 / NEC V30 software
├── contracts/     # exact cross-plane ABI/wire/binary contracts
├── tools/         # engineering/debug/analysis applications
├── scripts/       # repository/build automation
├── tests/         # verification organized by ownership
├── docs/          # durable documentation
├── cmake/         # CMake build primitives
├── third_party/   # external dependencies
└── .github/       # CI/workflow integration
```

Generated compiler state and staged artifacts are outputs, not source architecture:

```text
build*/
artifacts/
```

They must not become authoritative inputs for directory ownership.

## Execution planes

### `host/`

Production software executed on the PC/Host and required for normal RP86 operation.

```text
host/
├── rp86/          # reusable Host runtime library
└── apps/          # user-facing Host entry points
```

The runtime library owns Host-side broker, transport, device ownership, session/runtime state, workload lifecycle, and service clients.

`host/` must not depend on `tools/` or `tests/`.

### `firmware/`

Code executed by the RP2350 companion controller.

The existing internal split remains canonical:

```text
firmware/
├── board/
├── bus/
├── host_protocol/
├── memory/
├── runtime/
├── storage/
└── main.c
```

Firmware owns implementation of the Host Protocol and processor-facing resource/bus behavior, but cross-plane representations themselves belong to `contracts/` once migrated.

### `processor/`

Code executed natively by the physical Intel 8086 / NEC V30.

```text
processor/
├── include/
├── runtime/
├── ports/
└── workloads/
    ├── builtins/
    ├── examples/
    └── validation/
```

Portable-layer implementation and executable validation workloads remain distinct. For example:

```text
processor/ports/freertos/8086/
processor/workloads/validation/freertos_*/
```

are different ownership classes and must not be merged.

## Cross-plane contracts

`contracts/` is deliberately narrow. A definition belongs here only when two or more execution planes must agree on the same exact representation.

Initial contract domains:

```text
contracts/
├── host_protocol/
├── workload/
└── processor_abi/
```

Examples include:

- the fixed 64-byte Host Protocol record;
- operation/status identifiers and structured payload layouts;
- the `.P86W` manifest/package representation;
- processor-visible ports, vectors, memory-map constants, signatures, and shared-mailbox ABI.

`contracts/` must never become a general `common/`, `shared/`, `utils/`, or convenience-code directory.

Language-specific C, Python, and NASM consumers must either be generated from one authority or checked mechanically against it. Duplicated manually maintained definitions are not an acceptable long-term authority model.

## Engineering and automation

### `tools/`

Developer-facing engineering applications only: execution laboratories, diagnostics, trace/disassembly analysis, and artifact inspection.

Deleting `tools/` must not make the deployed RP86 system unable to operate normally.

The IA16 binary execution laboratory belongs here:

```text
tools/ia16_lab/
```

### `scripts/`

Non-interactive repository/build orchestration only: build drivers, dependency bootstrap, packaging/staging, CI helpers, and source-tree maintenance checks.

A file does not belong in `scripts/` merely because it is Python.

## Verification

Tests mirror source ownership first and test style second:

```text
tests/
├── host/
├── firmware/
├── processor/
├── tools/
├── build/
├── integration/
└── physical/
```

`integration/` is reserved for tests whose subject genuinely crosses ownership planes.

`physical/` remains a separate validation class because physical Intel 8086 / NEC V30 evidence is the final architectural acceptance layer, not an ordinary unit-test category.

## Documentation authority

```text
docs/architecture/   durable current system architecture
docs/reference/      exact stable technical reference and ABI/maps
docs/development/    build/debug/developer workflows
docs/validation/     acceptance methods and accepted evidence
docs/bringup/        hardware bring-up and recovery
docs/adr/            durable architectural decisions and rationale
```

The repository documentation rule is:

> README explains the system. Issues explain the journey. Code proves the current state.

Git history is the archive; superseded documentation should not create a parallel archive tree.

## Dependency rules

The repository must preserve these boundaries:

```text
production Host      -X-> tools
production Host      -X-> tests
firmware             -X-> tools
firmware             -X-> tests
processor            -X-> Host implementation
processor            -X-> firmware implementation internals
```

Cross-plane communication occurs through explicit contracts rather than implementation imports.

Engineering layers may inspect production artifacts:

```text
tools   -> production artifacts/libraries
scripts -> build/orchestrate source domains
tests   -> verify source domains/contracts
```

## Migration policy

Issue #82 performs the transition from the historical tree to this model.

The migration changes ownership paths, imports, build references, CI paths, and documentation links. It must not be used to change runtime semantics, fix FreeRTOS #75 behavior, redesign the #79 ISA validator, or broaden the #80 IA16 laboratory.

Temporary duplicate paths are allowed only inside the #82 migration branch while consumers are moved. They must not remain when #82 is accepted.
