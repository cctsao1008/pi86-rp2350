# Shared Technical Management Framework

The Pi86-RP2350 bring-up produced project-agnostic lessons that have now been promoted into the standalone cross-project repository:

[`cctsao1008/technical-management-framework`](https://github.com/cctsao1008/technical-management-framework)

That repository is the reusable source for:

- evidence and truth hierarchy;
- namespace and identity discipline;
- known-good baseline method;
- source-of-truth governance;
- diagnostic design and model-reset triggers;
- test acceptance criteria;
- project-local truth templates;
- ADR and postmortem templates;
- AI-assisted technical collaboration rules;
- cross-project case studies.

Pi86-specific facts remain owned by this repository, including:

- hardware contract and exact pin mapping;
- V30 bus implementation;
- bring-up gates and acceptance criteria;
- source code and commits;
- project issues and raw validation evidence.

Pi86-RP2350 is retained in the shared framework as **Case Study 001**, but it no longer owns the generalized methodology.

The legacy seed under [`docs/engineering_playbook/`](docs/engineering_playbook/README.md) is retained temporarily for provenance and migration history. New cross-project methodology changes should be made in `technical-management-framework`, not duplicated here.
