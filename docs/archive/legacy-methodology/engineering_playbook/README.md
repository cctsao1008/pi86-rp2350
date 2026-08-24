# Engineering Playbook Seed

This directory is a reusable, project-agnostic engineering methodology package extracted from the Pi86-RP2350 bring-up retrospective.

It is intentionally written so it can be moved into a standalone `engineering-playbook` repository without changing its meaning.

## Purpose

The playbook defines how to perform AI-assisted engineering work while preserving evidence provenance, namespace identity, known-good baselines, explicit hypotheses, discriminating tests, and trustworthy acceptance criteria.

## Core documents

- `engineering_truth_hierarchy.md`
- `namespace_identity_rules.md`
- `known_good_baseline.md`
- `diagnostic_design.md`
- `test_acceptance_criteria.md`
- `ai_engineering_collaboration_protocol.md`
- `migration_porting_checklist.md`
- `postmortem_template.md`
- `adr_template.md`

## Three-layer model

### Layer 1 — Project-local truth

Each project should maintain:

- hardware/software contracts
- architecture documents
- ADRs
- current status
- evidence matrix
- raw evidence links
- retrospectives

### Layer 2 — Engineering playbook

Reusable process guidance for:

- migrations and ports
- hardware/software debugging
- test design
- source hierarchy
- failure analysis
- postmortems

### Layer 3 — AI collaboration protocol

Rules for AI-assisted engineering:

- distinguish facts, hypotheses, inferences, and decisions
- preserve explicit namespaces
- respect known-good baselines
- state provenance and confidence boundaries
- trigger assumption reset when diagnostics stop converging
- validate functional postconditions rather than only code-path completion

## Design principle

The objective is not to prevent mistakes. The objective is to make mistakes observable, bounded, reversible, and reusable as organizational knowledge.
