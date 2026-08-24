# Migration / Porting Checklist

## 1. Define the known-good baseline

- Hardware/revision:
- Software/firmware commit:
- Toolchain/dependencies:
- Configuration/parameters:
- Evidence that baseline works:

## 2. Define the delta

### Unchanged

List components/interfaces assumed preserved.

### Changed

List host, target, mapping, API, driver, timing, toolchain, dependency, configuration, build, and environment changes.

## 3. Define identity namespaces

For every cross-system identifier, record:

- source namespace;
- canonical boundary identity;
- target namespace;
- authoritative mapping source.

## 4. Establish project truth artifacts

Before deep debugging, create or update:

- current status;
- interface/hardware/software contract;
- architecture document;
- ADRs for non-obvious decisions;
- evidence matrix;
- raw evidence archive.

## 5. Bring up one layer at a time

Recommended order:

1. platform/toolchain sanity;
2. identity/mapping;
3. safe initialization;
4. lowest-level observable transaction;
5. functional read path;
6. functional write path;
7. semantic execution;
8. completeness;
9. performance.

Do not optimize before correctness and observability exist.

## 6. Diagnostic discipline

- Separate evidence from hypotheses.
- Use discriminating tests.
- Define finite bounds and safe failure states.
- Re-open foundational assumptions after 2–3 non-converging tests.

## 7. Acceptance and closure

A milestone closes only when:

- functional postconditions pass;
- limitations are explicit;
- raw evidence is archived;
- relevant commit/build/version is recorded;
- superseded results are marked;
- next unvalidated layer is identified.
