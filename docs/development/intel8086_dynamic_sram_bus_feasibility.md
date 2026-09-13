# Intel 8086 Dynamic SRAM Bus Feasibility

## Status

Issue #106 feasibility design. This document does not change production runtime behavior and does not claim physical timing acceptance.

## Goal

Determine whether the current fixed-READY Pi86 HAT can support arbitrary internal-SRAM-backed Intel 8086 memory cycles while the processor clock runs continuously at an in-spec rate, starting at 2 MHz, without M33 participation inside the active bus cycle.

The required current-cycle path is:

```text
Intel 8086 address/control
        |
        v
PIO capture + classify
        |
        v
PIO repacks physical scattered address into a usable SRAM address
        |
        v
DMA control channel programs/starts data channel
        |
        +--------------------+
        |                    |
        v                    v
SRAM read -> PIO TX      PIO RX -> SRAM write
        |                    |
        v                    v
PIO drives AD          write committed
        |
        v
PIO releases AD
```

M33 may configure, arm, refill, observe and diagnose the engine outside active cycles. It must not compute the answer to a current no-wait-state memory cycle.

## 1. Established constraints

### 1.1 READY is not available

The current Pi86 HAT keeps processor READY asserted. The runtime therefore cannot extend a cycle with an ordinary Intel wait state. Any supported current cycle must complete within the fixed bus deadline.

### 1.2 First timing target is 2 MHz

The first feasibility target is continuous 2.000 MHz Intel operation:

```text
processor period = 500 ns
minimum four-state bus cycle = 2.0 us
```

The usable response interval is smaller than the complete bus cycle because read data must be driven and stable before the processor's sampling boundary. Exact acceptance margins remain part of #108 and physical timing validation.

### 1.3 The physical address/data pins are scattered

The Pi86 HAT wiring is not a packed GPIO bus. `AD0..AD15` are distributed across GPIO0..27 and `A16..A19` are also non-contiguous.

Therefore this invalid shortcut is forbidden:

```text
raw GPIO snapshot == linear 20-bit processor address
```

The realtime path needs an explicit hardware-paced repack stage before the processor address can become an SRAM address.

## 2. What RP2350 DMA can provide

RP2350 DMA provides the primitives needed for a hardware-paced control/data pipeline:

- peripheral DREQ pacing, including PIO FIFO DREQs;
- channel chaining;
- channel control-register aliases;
- trigger aliases for `READ_ADDR`, `WRITE_ADDR`, `TRANS_COUNT` and `CTRL`;
- a channel can configure and launch another channel by writing a compact control block or one trigger alias.

This means a control channel can consume a dynamically produced address word and write it into another DMA channel's `READ_ADDR_TRIG` or `WRITE_ADDR_TRIG` register.

RP2350 also retains a reload value for `TRANS_COUNT`. Each time a channel starts a new transfer sequence, that reload value is copied into the live counter. Therefore a one-word data channel can be programmed once with `TRANS_COUNT=1` and then started repeatedly by later writes to `READ_ADDR_TRIG` without an intervening M33 write to `TRANS_COUNT`.

A trigger received while the channel is already busy is ignored. Repeated dynamic transfers therefore still need a hardware ordering rule so that the next pointer is not published until the previous data transfer has completed.

Important limitation:

> DMA supplies transfer, chaining and register-programming machinery; it does not by itself decode the Pi86 HAT's scattered GPIO bitmap into a linear processor address or perform an arbitrary `SRAM_BASE + processor_address` arithmetic transform.

The address-generation problem must therefore be solved before the DMA data transfer is triggered.

## 3. Proposed read pipeline

The preferred initial experiment is a single internal-SRAM window whose processor-visible base is fixed.

### Stage R0 — PIO capture

At the accepted early-T1 point, PIO captures the complete raw GPIO bank while the multiplexed AD pins still contain `A15..A0` and the high-address pins contain `A19..A16`.

### Stage R1 — PIO address repack

PIO transforms the scattered physical pin bitmap into a contiguous processor address or directly into the full RP2350 SRAM source address.

The preferred output is the full DMA source pointer:

```text
0x2000_0000 + backing_offset + processor_offset
```

This avoids requiring DMA to perform base-address arithmetic.

This stage is the first major feasibility question. PIO v1 can execute at system-clock scale and has enough scratch/shift machinery to transform bit fields, but the actual instruction sequence and worst-case cycle count must be measured rather than assumed.

### Stage R2 — DMA control write

A PIO RX DREQ paces a control DMA transfer that writes the full source pointer into the data channel's `READ_ADDR_TRIG` alias.

The data channel is preconfigured with:

```text
WRITE_ADDR  = PIO responder TX FIFO
TRANS_COUNT = one word
TREQ_SEL    = appropriate pacing policy
```

Writing `READ_ADDR_TRIG` both installs the dynamic SRAM source pointer and launches the data transfer. The programmed one-word `TRANS_COUNT` reloads automatically on each later trigger.

### Stage R3 — SRAM fetch

The data channel reads the backing SRAM word and writes it to the responder PIO TX FIFO.

### Stage R4 — lane select / AD packing / drive

The responder PIO selects the required byte lanes, converts the memory word into the existing scattered GPIO output bitmap, enables AD only for the qualified read phase, and releases AD at the established safe boundary.

## 4. Proposed write pipeline

A write is similar but direction is reversed.

### Stage W0 — capture address and lane state

Capture and repack the processor address during T1.

### Stage W1 — install dynamic SRAM destination

A control DMA channel writes the full destination pointer into a data channel's `WRITE_ADDR_TRIG` alias.

### Stage W2 — capture write data

PIO captures D15..D0 during the write data phase and pushes the data word plus lane metadata to its RX path.

### Stage W3 — commit

The data DMA writes the byte/word into internal SRAM without an M33 callback.

Byte writes require either:

- separate 8-bit DMA transactions with dynamically selected byte destination; or
- a read-modify-write mechanism that is still fully hardware-paced.

The first experiment should prefer direct 8-bit DMA writes for low/high byte transactions to avoid introducing a realtime RMW dependency.

## 5. Critical feasibility risks

### Risk A — scattered-pin repack latency

The HAT wiring forces a non-trivial permutation from raw GPIO bits to `A19..A0` and from data words back to the scattered AD bitmap.

This is likely feasible at RP2350 PIO clock rates, but it must be demonstrated with a concrete instruction count and a timing margin relative to the Intel read-data deadline.

### Risk B — DMA launch latency

The control-channel write to a trigger alias, data-channel SRAM access and PIO FIFO delivery all consume finite bus/DREQ latency. The complete worst-case path must fit before PIO must drive valid read data.

### Risk C — RP2350 internal bus contention

The feasibility test must run firmware from SRAM exactly as production does, but SRAM banking, DMA priority and simultaneous CPU access can affect bounded latency. The experiment needs a deliberately controlled contention profile and later a pessimistic stress case.

### Risk D — byte lanes

`A0` and `UBE/BHE` define low-byte, high-byte and word cycles. The realtime engine must not collapse them into a naïve 16-bit access model.

### Risk E — prefetch and unsupported cycles

The engine must distinguish qualified memory cycles from I/O and INTA. Unsupported cycles must remain electrically high-Z and must not corrupt the DMA channel state machine.

## 6. Resource model

RP2350 provides three PIO blocks / twelve state machines and a system DMA capable of PIO DREQ pacing. The existing project already consumes PIO resources for clock, prepared response and service/INTA functions, so #106 must explicitly account for state-machine, instruction-memory and DMA-channel ownership rather than assuming unlimited resources.

The experiment should initially be isolated from the production service plane and use the minimum resource set needed to prove memory reads/writes.

## 7. Feasibility experiment phases

### Phase A — static transformation model

Before physical execution:

1. encode the canonical HAT pin map as a permutation table;
2. generate/read back randomized `A19..A0`, `A0/UBE`, and D15..D0 vectors;
3. prove that software reference pack/unpack functions are bijective for the supported lane cases;
4. define the exact PIO transformation sequence and instruction count.

The host-side permutation model and randomized round-trip tests are implemented in this PR.

### Phase B — DMA control-plane laboratory

Without the Intel processor driving the bus:

1. have PIO emit synthetic full SRAM addresses;
2. use PIO DREQ -> control DMA -> `READ_ADDR_TRIG`;
3. fetch randomized internal-SRAM words into a PIO TX FIFO;
4. repeat for dynamic `WRITE_ADDR_TRIG` writes;
5. verify no M33 callback is used per transfer.

Phase B is split into two read-side substeps:

```text
B1  one-shot dynamic READ_ADDR_TRIG primitive
B2  repeated hardware-autonomous reads with PIO/DMA handshake
```

The B2 implementation uses four DMA roles and two PIO state machines:

```text
pointer array
    -> feeder DMA
    -> source PIO
    -> control DMA
    -> data READ_ADDR_TRIG
    -> SRAM word
    -> sink PIO
    -> capture DMA
    -> result array
```

The source PIO waits for a sink-PIO IRQ acknowledgement before publishing the next pointer. This closes the trigger-while-busy race without M33 rearming individual transfers.

The laboratory source now expresses this topology, but physical execution of the lab target is still required before B2 can be marked proven.

### Phase C — timing-bound synthetic bus

Drive synthetic ASTB/address/control sequences into the realtime front end and measure:

```text
early-T1 sample
    -> address repacked
    -> DMA trigger programmed
    -> SRAM word fetched
    -> PIO data available
    -> AD drive point
```

Reject the design if the worst-case path cannot meet the #108 deadline with margin.

### Phase D — physical Intel finite workload

Only after A-C pass:

- continuous 2 MHz clock;
- finite randomized SRAM read/write workload;
- low-byte, high-byte and aligned-word cases;
- trace address/data agreement;
- no Host/M33 current-cycle service;
- deterministic completion/fault cleanup.

FreeRTOS #75 remains outside this phase.

## 8. Decision gate

Issue #106 passes only if the project can demonstrate all of the following:

```text
scattered physical address
        -> deterministic PIO repack
        -> hardware-paced DMA dynamic address install
        -> internal SRAM read/write
        -> valid bus response
```

within the continuous Intel timing contract and without M33 inside the active cycle.

If any required step needs an unbounded software callback, or if worst-case timing cannot fit with defensible margin, the existing HAT cannot be treated as a compliant arbitrary-memory Intel runtime. The architectural fallback is controllable READY / external wait-state hardware or another deterministic hardware datapath.

## 9. Immediate next implementation step

Do not change `RP86_PROCESSOR_HZ` yet.

The immediate gate is now the physical engineering target:

```text
rp86_issue106_dma_alias_read_lab
```

It must prove the full B2 finite stream on RP2350:

```text
PIO/DMA pointer stream
    -> repeated READ_ADDR_TRIG
    -> arbitrary SRAM reads
    -> PIO sink
    -> DMA-captured results
```

with no M33 per-transaction rearm. After that, implement the symmetric repeated `WRITE_ADDR_TRIG` laboratory before integrating the scattered GPIO address front end.

## References

- RP2350 Datasheet, DMA chapter: `TRANS_COUNT` reload semantics, channel chaining, peripheral DREQ pacing and control-register trigger aliases.
- RP2350 Datasheet, PIO chapter: RP2350 PIO v1 features, three PIO blocks / twelve state machines, reduced DREQ latency.
- Raspberry Pi Pico-series C/C++ SDK: `pio_get_dreq()` and DMA/PIO integration APIs.
- `firmware/bus/processor_bus_pins.h`: canonical scattered Pi86 HAT signal map.
- `firmware/runtime/prepared_bus_responder.pio`: accepted early-T1 sampling and AD ownership timing pattern.
- `firmware/runtime/processor_service.pio`: accepted PIO-owned two-cycle INTA response pattern.
- ADR 0011 and `docs/architecture/intel_8086_clock_contract.md`: normative Intel continuous-clock boundary.
- Issue #108: exact Intel timing budget and service classification.
