# Namespace and Identity Rules

## Principle

Every identifier belongs to a namespace. Similar numeric or textual values across namespaces are not interchangeable.

Examples:

- WiringPi number vs BCM GPIO vs physical connector pin vs MCU GPIO
- virtual address vs physical address
- marketing part number vs silicon revision
- Jira issue vs firmware build vs Git commit
- release label vs tag vs commit SHA vs binary hash
- logical device name vs physical instance

## Required practice

1. Define namespaces before mapping between systems.
2. Use qualified names in documents and logs.
3. Define a canonical identity at physical or contract boundaries.
4. Make translation edges explicit.
5. Never rely on equal-looking values as proof of identity.
6. For generated code/configuration, centralize mappings in one source of truth.

## Mapping table pattern

Use:

| Entity | Source namespace | Boundary/canonical identity | Target namespace | Provenance |
|---|---|---|---|---|

Do not use ambiguous labels such as `GPIO9`, `v1.4`, `device 3`, or `port 2` without a namespace.

## Review rule

Before analyzing behavior of an entity, prove its identity first.

`identity -> ownership/direction -> state -> timing -> protocol meaning`
