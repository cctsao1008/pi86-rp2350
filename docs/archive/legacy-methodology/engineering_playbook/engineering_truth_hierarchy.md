# Engineering Truth Hierarchy

## Rule

When sources disagree, do not flatten them into one narrative. Rank them and preserve provenance.

Default priority:

1. Verified behavior of the actual system under test.
2. Current target-specific official documentation, schematic, API contract, or datasheet.
3. Project-owned source-of-truth documents and executable configuration.
4. Current upstream/reference implementation.
5. Historical artifacts and archived revisions.
6. Derived interpretation.
7. Unverified hypothesis.

A lower-priority source must not silently override higher-priority evidence.

## Required labels

Engineering notes should distinguish:

- **FACT** — directly observed or directly supported by an authoritative source.
- **INFERENCE** — derived from facts but not directly observed.
- **HYPOTHESIS** — candidate explanation requiring discrimination.
- **DECISION** — chosen engineering action or architecture rule.
- **INVALIDATED** — previously plausible interpretation rejected by later evidence.

## Conflict handling

When two sources conflict:

1. identify the exact conflicting claim;
2. state each source and its scope/revision;
3. determine whether they describe different targets, versions, namespaces, or operating modes;
4. prefer target-specific and experimentally verified evidence;
5. record the resolution and superseded interpretation.

## Anti-pattern

Do not increase confidence merely because many documents repeat the same intermediate mapping or assumption. Evidence density is not evidence independence.
