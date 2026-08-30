# Physical processor software

This directory contains every program executed natively by the installed
Intel 8086 or NEC V30. It is intentionally separate from `firmware/`, which
contains only software executed by the RP2350.

```text
runtime/    resident processor-side runtime embedded in the RP2350 firmware
workloads/  independently loadable native programs and validation images
```

The processor executes these instructions on its physical bus. The RP2350
provides clock, reset, memory, I/O, interrupt, and observation services; it
does not interpret the instruction stream.

See [`workloads/README.md`](workloads/README.md) for source, metadata, and
package rules.
