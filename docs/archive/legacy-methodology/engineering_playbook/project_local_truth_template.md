# Project-Local Engineering Truth Template

Each engineering project should expose a small set of authoritative entry points so future work does not depend on chat history or individual memory.

## Required artifacts

### 1. Current Status

- current milestone
- validated capabilities
- open risks
- known limitations
- next decision point
- exact current build/commit where relevant

### 2. Interface / Hardware / Software Contract

Define invariants, canonical identities, mappings, ownership rules, compatibility boundaries, and non-negotiable assumptions.

### 3. Architecture

Define major modules, critical paths, replaceable boundaries, dependencies, and failure containment.

### 4. ADR directory

Record decisions whose rationale would otherwise be lost.

### 5. Evidence Matrix

One row per gate/milestone/test containing:

- capability;
- test target;
- source/build/commit;
- status;
- key evidence;
- raw evidence link;
- interpretation boundary.

### 6. Raw Evidence Archive

Preserve logs, captures, screenshots, measurements, binaries/hashes, and hardware observations separately from narrative summaries.

### 7. Retrospectives

Capture expensive mistakes, invalidated models, corrective actions, and reusable rules.

## Discoverability requirement

README or project index must link to all authoritative artifacts above. A correct document that cannot be found later is only partially useful.

## Change discipline

When a foundational truth changes, update all dependent artifacts together and mark older evidence as superseded rather than silently deleting history.
