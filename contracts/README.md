# Cross-plane contracts

`contracts/` is the designated ownership boundary for exact representations that must agree across more than one RP86 execution plane.

This directory is intentionally narrow. It is not a general `common/`, `shared/`, `utils/`, or convenience-code bucket.

A definition belongs here only when two or more of Host, RP2350 firmware, and physical 8086/V30 software must agree on the same wire, binary, memory-map, or ABI representation.

The contract domains identified by #82 are:

- `host_protocol/` — the fixed Host Protocol record layout, operation/status identifiers, and structured payload ABI.
- `workload/` — the `.P86W` workload package/manifest representation and workload lifecycle wire values.
- `processor_abi/` — processor-visible ports, vectors, memory-map constants, signatures, and shared-mailbox ABI.

## Migration rule

Do not copy existing C, Python, or NASM definitions here merely to make the directory look complete; that would create a second source of truth. A contract moves into this boundary only when one authoritative representation can be established and every language-specific consumer is generated from it or checked mechanically against it.

Until a contract is migrated in that form, its current executable definition remains authoritative and the existing cross-language consistency tests remain mandatory evidence against drift. The repository structure established by #82 therefore defines the ownership destination without pretending that every cross-plane representation has already been consolidated.
