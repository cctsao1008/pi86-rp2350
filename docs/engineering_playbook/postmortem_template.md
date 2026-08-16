# Engineering Postmortem Template

## Executive Summary

What failed, what the real root cause was, and the current status.

## Known-Good Baseline

What was already proven to work before the change.

## Intended Change

What was being migrated, ported, redesigned, or replaced.

## What Changed / What Did Not Change

### Changed

- ...

### Unchanged

- ...

## Timeline

Record major observations, decisions, diagnostics, corrections, and validation milestones.

## Primary Root Cause

State the root cause at the correct abstraction layer.

## Contributing Factors

Include process, documentation, tooling, communication, test-design, source-quality, and reasoning contributors.

## Invalid or Superseded Diagnostic Paths

For each:

- observation;
- original interpretation;
- hidden assumption;
- why it was invalidated;
- what evidence corrected it.

## Test-Design Errors

Document false PASS criteria, invalid assumptions, insufficient observability, and semantic-layer mistakes.

## Communication / Reasoning Errors

Document overconfident claims, namespace ambiguity, assumption drift, or source-provenance failures.

## Corrective Actions

### Project-local

- contracts
- mappings
- tests
- architecture
- documentation

### Reusable process

- playbook rules
- templates
- automation/checks

## Permanent Rules

Short statements that should survive the project.

## Validated Evidence Chain

List what is now proven, in order, with links to raw evidence and commits.

## Remaining Interpretation Boundaries

Explicitly state what the successful tests do **not** establish.
