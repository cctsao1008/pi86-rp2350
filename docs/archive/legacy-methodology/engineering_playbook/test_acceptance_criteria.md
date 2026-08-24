# Test Acceptance Criteria

## Principle

A test passes only when the required functional postcondition is demonstrated. Reaching the end of a code path is not sufficient.

## Acceptance criteria checklist

Each test must define:

- initial state;
- stimulus;
- observable outputs;
- required postconditions;
- forbidden states or failure signatures;
- timeout / finite bound;
- safe failure state;
- evidence to archive.

## Result classes

Use explicit result classes where useful:

- PASS — all required postconditions observed.
- FAIL — at least one required postcondition violated.
- INCONCLUSIVE — execution completed but evidence cannot distinguish hypotheses.
- INVALID — test assumptions, mapping, fixture, or instrumentation were wrong.
- SUPERSEDED — result was valid for its setup but later evidence shows the setup did not represent the intended system.

## Semantic layers

Do not assume a high-level behavior maps one-to-one to a low-level trace. Account for intermediate mechanisms such as:

- CPU prefetch / pipelines;
- caches;
- DMA;
- buffering;
- retries;
- scheduling;
- protocol framing;
- compiler/runtime transformations.

Acceptance criteria must be written at the layer actually being tested.

## Evidence rule

Whenever possible, archive both the summarized verdict and the raw observation that produced it.
