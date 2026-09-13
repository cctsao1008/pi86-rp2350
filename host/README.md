# Host software plane

`host/` contains production software that runs on the PC/Host and participates in normal RP86 operation.

## Ownership

- `host/rp86/` — reusable Host runtime library: broker, transport, protocol client, workload lifecycle, device ownership, runtime/session state, and Host-side service helpers.
- `host/apps/cli/` — canonical local CLI entry point and shell.
- `host/apps/web/` — local Web console implementation.
- `host/apps/remote/` — network-facing Host application entry points.

## Boundary

Production Host code may depend on cross-plane contracts and may talk to RP2350 firmware through the Host Protocol. It must not depend on `tools/`, `tests/`, or physical-test implementations.

The canonical CLI entry point is:

```text
host/apps/cli/rp86.py
```

Historical `tools/rp86*` locations are migration residue and are not authoritative production paths.
