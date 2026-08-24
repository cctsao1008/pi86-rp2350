# Known-Good Baseline Method

## Purpose

A migration, port, refactor, board spin, toolchain change, or supplier replacement should begin by explicitly defining the known-good baseline.

## Baseline record

Capture:

- hardware identity and revision
- firmware/software version or commit
- toolchain and dependency versions
- configuration and parameters
- test procedure
- observed working behavior
- evidence location

## Delta table

Before debugging the new system, write two lists:

### Unchanged
Components, interfaces, and behaviors inherited from the known-good system.

### Changed
Every target, mapping, implementation, timing, dependency, configuration, toolchain, or environment change.

Initial debugging priority should follow the changed set unless evidence specifically implicates an unchanged component.

## Prior rule

A known-good component is not infallible, but it carries a strong prior. Reclassifying it as defective requires new evidence that also explains why the baseline previously worked.

## Regression rule

When possible, preserve an A/B path back to the known-good configuration so assumptions can be rechecked without redesigning the experiment.
