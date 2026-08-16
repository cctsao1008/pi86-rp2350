# Diagnostic Design Rules

## Objective

A diagnostic should discriminate between competing hypotheses. It should not merely generate more data.

## Required issue structure

For each investigation, record:

- Observed Evidence
- Hypotheses
- Invalidated Hypotheses
- Current Decision
- Next Discriminating Test

## Good diagnostic properties

A good test:

- changes one meaningful variable at a time;
- has an explicit expected result for each hypothesis;
- preserves a safe failure state;
- records raw evidence before interpretation;
- minimizes dependence on unverified assumptions;
- can be repeated from a known initial state.

## Model-reset trigger

Stop deepening the current diagnostic tree and reopen foundational assumptions when any of these occur:

- 2–3 successive diagnostics fail to converge;
- explanations require increasingly complex special cases;
- observations contradict multiple supposedly established facts;
- signal/entity identity has not been independently verified;
- a new test depends on more assumptions than the hypothesis it is meant to test.

Revalidate, in order:

1. identity / namespace;
2. version / revision;
3. orientation / topology;
4. units / polarity / encoding;
5. ownership / direction;
6. clock or timing domain;
7. source provenance;
8. known-good baseline.

## Anti-pattern

Do not become increasingly precise while debugging the wrong abstraction, signal, revision, or system configuration.
