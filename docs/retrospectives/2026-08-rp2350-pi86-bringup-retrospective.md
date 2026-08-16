# Pi86-RP2350 Bring-up Retrospective — 2026-08

## Executive summary

The RP2350 port initially produced a long series of contradictory observations even though the original Pi86 V20/V30 HAT and installed NEC V30 were already known to work on a Raspberry Pi with an older Pi86 baseline.

The primary root cause was not a V30 electrical defect, HAT failure or fundamental bus-timing incompatibility. It was a **host GPIO translation error**: Raspberry Pi BCM GPIO numbers were incorrectly treated as Waveshare RP2350-PiZero GPIO numbers.

Because the firmware used the wrong signal identities, several diagnostic programs accurately measured or drove the wrong physical nets. This generated plausible but invalid hypotheses around AD7 failure, ALE/ASTB behavior, contention and bus turnaround.

Once the mapping was rebuilt from the Raspberry Pi physical header position to the RP2350-PiZero GPIO, the system progressed quickly through deterministic verification:

```text
Gate 3: RESET -> first fetch 0xFFFF0                         PASS
Gate 4: aligned memory read + prefetch-aware execution       PASS
Gate 5: SRAM-backed executable ROM + far jump to F000:0000   PASS
Gate 6: aligned RAM write/readback + CPU compare/branch       PASS
```

The principal lesson is that AI-assisted engineering can generate many technically plausible explanations around an incorrect foundational model. Evidence density is not a substitute for validating the identity of the signal or interface being reasoned about.

---

## 1. Known-good baseline that should have dominated the investigation

The strongest prior available throughout the bring-up was:

```text
same Pi86 V20/V30 HAT
+ same NEC V30 D70116C-8
+ Raspberry Pi
+ older working Pi86 baseline
= operational system
```

The RP2350 port changed the host platform and firmware while preserving the HAT and CPU.

Therefore the initial delta set should have been constrained to:

- physical-header-to-host-GPIO translation,
- GPIO configuration and ownership,
- software-stepped clock implementation,
- RESET sequencing,
- ALE/address/control sampling,
- AD bus direction and service timing.

Instead, some diagnostic branches temporarily elevated hypotheses such as a defective AD7 path or physical HAT loading despite the known-good baseline.

### Corrective rule

A known-good hardware assembly is a strong prior. New-port failures should first be localized to the changed layers. A hardware-fault hypothesis is allowed, but it must explain how the new evidence remains compatible with the established working baseline.

---

## 2. Primary root cause: numbering-domain conflation

The project involved four distinct identifiers:

```text
WiringPi number
Raspberry Pi BCM GPIO number
Raspberry Pi physical header pin
RP2350 GPIO number
```

They were not treated as separate namespaces early enough.

The original Pi86 software uses WiringPi numbering. Converting that to Raspberry Pi BCM numbering correctly identified where the original Raspberry Pi drove a signal, but a further invalid assumption was made: the BCM number was treated as though it were the RP2350 GPIO number on the Waveshare board.

Example:

```text
RPi physical pin 21 = BCM9
```

was incorrectly interpreted as:

```text
RP2350 GPIO9
```

The actual RP2350-PiZero mapping is:

```text
RPi physical pin 21 = RP2350 GPIO12
```

This affected multiple V30 signals. The corrected values include:

```text
ALE   GPIO12 -> GPIO9
AD4    GPIO5 -> GPIO15
AD6   GPIO11 -> GPIO10
AD7    GPIO9 -> GPIO12
AD8   GPIO10 -> GPIO11
AD12   GPIO4 -> GPIO14
AD15  GPIO14 -> GPIO4
A16   GPIO15 -> GPIO5
```

### Why this failure was expensive

The mapping was internally coherent enough to let firmware compile, GPIO tests run and diagnostics print credible values. The wrong abstraction did not fail loudly; it produced believable but misidentified evidence.

### Corrective rule

The physical connector is the portable hardware ABI:

```text
V30 signal -> RPi physical pin -> target-board routing -> RP2350 GPIO
```

BCM and WiringPi are reference-platform metadata only.

---

## 3. Signal identity was not proven before signal behavior was analyzed

One diagnostic observed:

```text
SIO OUT = 1
OE      = 1
PAD     = 0
```

The software believed the tested node was AD7. This led to hypotheses involving:

- physical AD7 loading,
- contention,
- V30 bus ownership,
- RP2350 pad behavior,
- turnaround delay.

After the mapping correction, the firmware was shown to have been operating a different physical signal.

The observation itself was real. The assigned signal identity was wrong.

### Corrective rule

Debugging order must be:

```text
identity -> ownership/direction -> electrical state -> timing -> protocol interpretation
```

Do not analyze why "AD7" is low until it has been independently demonstrated that the sampled GPIO is physically connected to AD7.

---

## 4. Too many second-layer diagnostics were built on one unverified first-layer assumption

The project accumulated increasingly focused tests such as:

- AD line diagnostics,
- AD state isolation,
- phase scans,
- ASTB/ALE verification,
- DMA capture,
- data-phase diagnostics,
- AD7 RESET-vs-active A/B tests,
- turnaround-delay sweeps.

Many were technically useful test patterns. However, they inherited the same incorrect mapping.

This is a classic case of **precisely debugging the wrong system**.

### Missed escalation trigger

Several observations became mutually awkward:

- reset-vector evidence appeared partially correct,
- ALE behavior appeared abnormal,
- AD7 appeared persistently low,
- pattern-dependent data failures appeared,
- RESET-state observations did not fit one clean electrical model.

The proper response after a small number of contradictory diagnostics should have been to invalidate foundational assumptions, not add more diagnostic depth.

### Corrective rule

After two or three focused diagnostics fail to converge on a coherent model, explicitly reopen:

1. signal identity,
2. board revision/source identity,
3. numbering translations,
4. test acceptance criteria,
5. assumptions inherited by all diagnostics.

---

## 5. Evidence density created false confidence

The investigation consulted many legitimate sources:

- original Pi86 `x86.h`,
- original Pi86 `x86.cpp`,
- WiringPi mapping,
- KiCad PCB/netlist data,
- NEC V30 manual,
- RP2350-PiZero schematic/pinout,
- serial logs and hardware captures.

The problem was not a lack of sources. The problem was an unsupported transition between two source domains.

A reasoning chain effectively became:

```text
multiple Pi86 sources corroborate BCM9
therefore
RP2350 GPIO9
```

The first statement could be heavily sourced while the second remained unverified.

### Corrective rule

For every cross-platform mapping, document each translation edge separately. Confidence in upstream evidence must not leak across an unverified boundary.

---

## 6. Source hierarchy was not strict enough

The target-board physical pinout should have been consulted earlier and given stronger authority for the final translation.

The preferred hierarchy is now:

1. physical working-system behavior,
2. target-board official pinout/schematic,
3. HAT PCB routing,
4. original Pi86 software,
5. WiringPi/BCM translation,
6. historical schematics/netlists,
7. inference.

No lower-level source should silently override higher-level physical evidence.

---

## 7. Test-design error: PASS checked execution path, not output correctness

An early Gate 4 test requested:

```text
0xFEEB
```

but observed:

```text
0xFE6B
```

and still printed an overall PASS because its final predicate effectively checked that the service sequence had completed rather than verifying that the driven/read-back data matched the request.

### Lesson

A test is not correct because the test code ran to completion.

Acceptance criteria must test the externally meaningful postcondition.

For a memory read cycle this means at minimum:

```text
correct address
+ correct transaction classification
+ correct data
+ correct bus release
```

not merely `read_cycle_completed == true`.

---

## 8. Test-design error: CPU semantics were confused with immediate bus order

A repeated `EB FE` (`JMP SHORT -2`) test initially assumed that every subsequent external read should immediately return to `0xFFFF0`.

The observed sequence was:

```text
FFFF0
FFFF2
FFFF4
FFFF0
...
```

The initial test classified `FFFF2` as a failure.

This ignored instruction prefetch. The CPU may issue sequential fetches before the branch is executed and the queue is refilled.

After making the test prefetch-aware, the sequence became useful positive evidence of CPU execution rather than a failure.

### Corrective rule

Do not equate architectural instruction semantics directly with external bus ordering. Explicitly model:

- prefetch,
- pipeline/queue behavior,
- bus turnaround,
- instruction execution boundaries,
- observable transaction timing.

---

## 9. Communication error: conclusions were stated too strongly

At several points observations that only supported a hypothesis were described with wording close to "this proves" or "the root cause is".

Hardware debugging requires explicit separation of:

```text
observation
interpretation
hypothesis
confirmed root cause
```

For example:

```text
OUT=1, OE=1, PAD=0
```

was an observation.

"External contention on AD7" was only one interpretation — and, because the signal identity itself was wrong, it was not a valid root-cause conclusion.

### Corrective rule

Diagnostic notes should state the evidence first and tag conclusions by confidence:

- OBSERVED
- CONSISTENT WITH
- HYPOTHESIS
- RULED OUT
- CONFIRMED

---

## 10. Communication error: a user constraint was acknowledged but not fully encoded

The project repeatedly emphasized that the Raspberry Pi **physical pin** should be the canonical reference.

The idea was verbally accepted, but documentation and code still used ambiguous expressions such as combining "BCM/RP2350 GPIO" into one field.

This exposed an important collaboration failure mode:

> Agreement in conversation is not the same as encoding a constraint into the artifact.

### Corrective rule

When a constraint is important enough to affect correctness, turn it into an enforceable representation:

- dedicated document,
- explicit table columns,
- naming rules,
- code constants,
- review checklist.

---

## 11. What worked well after the reset of the model

After the RP2350-PiZero physical pinout corrected the mapping, the project deliberately rebuilt the evidence chain from the bottom instead of declaring victory from a single successful transaction.

### Gate 3

Observed a stable first physical address:

```text
0xFFFF0
```

across the ALE/T1 window.

### Gate 4

Demonstrated repeated aligned memory reads with correct data-pad readback and a prefetch-aware short-jump loop:

```text
FFFF0 -> FEEB
FFFF2 -> 9090
FFFF4 -> 9090
FFFF0 -> FEEB
```

### Gate 5

Replaced the hard-coded reset-vector loop with a real SRAM-backed ROM:

```text
FFFF0: JMP FAR F000:0000
F0000: NOP, NOP, JMP -4
```

The CPU repeatedly fetched from physical `0xF0000`, proving actual code execution from the RP2350-backed ROM.

### Gate 6

Executed a CPU-side RAM semantic test:

```text
MOV AX,1234
MOV [0200],AX
MOV AX,[0200]
CMP AX,1234
JNE FAIL
SUCCESS
```

The trace showed:

```text
WRITE 0x00200 = 0x1234
READ  0x00200 = 0x1234
SUCCESS loop reached
FAIL loop not observed
```

This is substantially stronger than host-side loopback because the V30 itself consumed the data and selected the success branch.

---

## 12. Permanent corrective actions

### Hardware/interface

- Treat physical header position as the hardware ABI.
- Maintain `docs/hardware_contract.md`.
- Keep `firmware/v30/v30_pins.h` consistent with the hardware contract.
- Prohibit undocumented raw V30 GPIO literals.
- Audit PIO files separately for absolute GPIO references.

### Test design

- Define acceptance criteria before writing the diagnostic.
- Check output postconditions, not just control-flow completion.
- Preserve raw observations in logs.
- Avoid embedding unsupported CPU bus-order assumptions.
- Mark diagnostics executed under invalid assumptions as superseded rather than silently deleting them.

### Reasoning/debugging

- Maintain a known-good baseline document.
- Track changed layers explicitly for ports.
- Re-open foundational assumptions after contradictory diagnostics.
- Separate evidence from inference.
- Prefer model-reset tests over increasingly elaborate patches to an unstable hypothesis.

### Documentation

- Use ADRs for decisions that protect against recurring classes of error.
- Use GitHub Issues for debugging archaeology and resolution history.
- Use Google Drive for raw evidence, captures and large source documents.
- Use a Bring-up Evidence Matrix to connect Gate, firmware commit, hardware result and evidence artifact.

---

## 13. Rules for future hardware ports

1. **Start with the connector, not the software GPIO number.**
2. **Record every numbering namespace explicitly.**
3. **Carry known-good behavior forward as a strong prior.**
4. **Verify signal identity before diagnosing signal behavior.**
5. **Do not let multiple citations hide an unsupported translation edge.**
6. **After repeated contradictory results, invalidate assumptions before adding diagnostics.**
7. **Make PASS predicates test the actual functional requirement.**
8. **Distinguish instruction semantics from bus-level behavior.**
9. **Encode important conversational constraints into version-controlled artifacts.**
10. **Preserve failed reasoning paths when they teach a reusable engineering lesson.**

---

## 14. Final assessment

The expensive part of this bring-up was not that the V30 bus was intrinsically intractable. The expensive part was allowing an incorrect interface identity model to survive long enough that increasingly sophisticated diagnostics were built on top of it.

The strongest reusable lesson for AI-assisted engineering is:

> A system can produce large quantities of internally consistent evidence while the model identifying the system is wrong.

The first responsibility of the debugging process is therefore not to explain every observation. It is to verify that the observation belongs to the signal, component and abstraction layer we think it does.

The corrected Gate 3–6 evidence now provides a clean foundation for further development, but the postmortem should remain part of the project because preventing recurrence is more valuable than merely recording that the bug was fixed.
