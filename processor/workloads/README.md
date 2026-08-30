# Native workloads

Each workload is assembled as a flat 16-bit binary and may be packaged as a
CRC-protected `.p86w` file.

```text
builtins/    maintained workloads shipped with the RP86 runtime
examples/    small public examples of workload and service interfaces
validation/  programs whose purpose is physical hardware validation
```

## Source and package model

A packaged workload has two version-controlled inputs:

- an `.asm` file containing native Intel 8086/NEC V30 instructions;
- a JSON file declaring load address, entry point, stack, clock mode, and
  optional runtime capabilities.

CRC32 and image size are derived from the assembled bytes. They are stored in
the generated package manifest and must never be copied into JSON by hand.

The normal build writes packages to:

```text
build/workloads/*.P86W
```

Source and metadata belong in Git. Generated `.bin` and `.p86w` files do not;
release-ready packages may be attached to a GitHub Release and frequently used
packages may be copied to `flash:/`.

## Metadata fields

```json
{
  "format": "p86w-v1",
  "name": "example",
  "load_address": "0x10000",
  "entry": "1000:0000",
  "stack": "0000:0000",
  "clock": "clock-stepped",
  "flags": ["stdio", "persistent"],
  "shared_memory": {"base": "0x3F000", "size": "0x1000"}
}
```

Supported clock values are `auto`, `free-running`, and `clock-stepped`.
Supported capability flags are `stdio`, `persistent`, and `shared-memory`.
The packager adds `shared-memory` automatically when `shared_memory` is
present.
