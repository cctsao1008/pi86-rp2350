# Host software plane

`host/` contains production software that runs on the PC/Host and participates in normal RP86 operation.

## Ownership

- `host/rp86/` — reusable Host runtime library: broker, transport, protocol client, workload lifecycle, device ownership, runtime/session state, and Host-side service helpers.
- `host/apps/` — user-facing Host applications and entry points. These will be migrated from the historical `tools/` locations in later #82 phases.

## Boundary

Production Host code may depend on cross-plane contracts and may talk to RP2350 firmware through the Host Protocol. It must not depend on `tools/`, `tests/`, or physical-test implementations.

During the #82 migration, historical `tools/rp86_runtime/` remains temporarily present until imports, tests, workflows, and entry points have moved. The duplicate path is transitional and must be removed before #82 is accepted.
