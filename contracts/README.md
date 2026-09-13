# Cross-plane contracts

`contracts/` owns exact representations that must agree across more than one RP86 execution plane.

This directory is intentionally narrow. It is not a general `common/`, `shared/`, `utils/`, or convenience-code bucket.

A definition belongs here only when two or more of Host, RP2350 firmware, and physical 8086/V30 software must agree on the same wire, binary, memory-map, or ABI representation.

Initial contract domains identified by #82 are:

- `host_protocol/` — the fixed Host Protocol record layout, operation/status identifiers, and structured payload ABI.
- `workload/` — the `.P86W` workload package/manifest representation and workload lifecycle wire values.
- `processor_abi/` — processor-visible ports, vectors, memory-map constants, signatures, and shared-mailbox ABI.

## Migration rule

Existing C, Python, and NASM definitions are not copied here merely to create another source of truth. #82 must first move or generate authoritative definitions so each contract has one ownership point and language-specific consumers remain mechanically checked.

Until that migration is complete, the current cross-language consistency tests remain mandatory evidence against drift.
