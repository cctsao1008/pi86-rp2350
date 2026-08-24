# Physical Bring-Up and Validation

> **Document role: CURRENT OPERATOR ENTRYPOINT.**
>
> This guide describes how to build, flash, observe, and accept a present-day
> `pi86-rp2350` physical experiment. The original Gate 0–12 development record
> is preserved in [`bringup/gate_history.md`](bringup/gate_history.md).

## Safety and authority

Before connecting or powering the V30 HAT, read:

1. [`hardware_contract.md`](hardware_contract.md) - canonical physical interface;
2. [`pin_mapping.md`](pin_mapping.md) - signal mapping through the 40-pin header;
3. [`hardware.md`](hardware.md) - board and voltage notes;
4. [`pi86_hat_design_review.md`](pi86_hat_design_review.md) - present HAT limitations.

The Raspberry Pi **physical header position** is the hardware ABI. Raspberry Pi BCM numbers and Waveshare RP2350 GPIO numbers are not interchangeable.

Never run a GPIO output-sweep target while the V30 HAT is installed. Such fixtures deliberately drive many pins and are only for an isolated RP2350 board with the intended LED/test fixture.

## Host responsibilities

The supported development workflow separates build and physical validation:

```text
WSL clone
  source, submodules, NASM, Pico SDK build, UF2

Windows clone
  USB CDC/HID discovery, request transport, raw evidence, acceptance
```

The canonical build instructions are in [`development/build_and_toolchain.md`](development/build_and_toolchain.md). The canonical Windows workflow is [`development/windows_physical_validation.md`](development/windows_physical_validation.md).

## 1. Select one bounded experiment

Before building, identify:

- the exact CMake target;
- the physical capability being tested;
- the expected configured V30 clock;
- the matching validation profile or manual acceptance contract;
- the known-good regression that must remain intact;
- the one new unknown introduced by the experiment.

Use a roughly **90% accepted foundation / 10% meaningful unknown** challenge budget. Do not combine a new bus engine, new USB protocol, new BIOS image, and new hardware interface in one gate unless the experiment is specifically designed to isolate all four.

## 2. Synchronize and build in WSL

```bash
cd ~/github/pi86-rp2350
git pull origin main
git submodule update --init --recursive
./scripts/bootstrap_tools.sh
./scripts/build.sh --target <target>
```

Record before flashing:

```bash
git rev-parse --short HEAD
sha256sum build/**/<target>.uf2
```

Use the exact output path printed by the build. Do not rename a UF2 in a way that hides its target or commit identity.

## 3. Prepare the hardware

With power removed:

1. verify HAT orientation and pin-1 alignment;
2. inspect for offset headers, bent pins, shorts, and loose jumpers;
3. confirm that the experiment matches the installed NEC V30 and current HAT;
4. remove unrelated GPIO fixtures;
5. connect only the probes and host interfaces required by the experiment.

At reset and before response-engine release, only explicitly authorized output pins may be driven. A bounded validation target normally ends at:

```text
RESET = HIGH
CLK   = LOW
AD    = high-Z
```

A persistent profile instead remains accepted only while the V30 is in proven
`STI`/`HLT` idle, AD remains high-Z between serviced cycles, and the interrupt
heartbeat and bus-safety evidence remain valid.

## 4. Flash the exact UF2

Enter the RP2350 ROM bootloader using the board's BOOT/RESET procedure, copy the selected UF2 to the enumerated mass-storage device, and wait for re-enumeration.

USB interface composition may change between firmware targets. Windows can assign a new COM number after flashing a CDC/HID composite image. Rediscover the device instead of assuming the previous port remains valid.

## 5. Arm the Windows validator

In the synchronized Windows clone:

```powershell
cd D:\my-github\pi86-rp2350
git pull origin main
py -m pip install -r tools\ai_bridge\requirements.txt
py tools\ai_bridge\physical_validator.py --list-ports
py tools\ai_bridge\v30bridge.py --list-devices
```

Run the exact named profile or bridge command documented for the target. For the accepted composite Host Bridge:

```powershell
py tools\ai_bridge\v30bridge.py --exchange --port COM<n> `
  --output-dir D:\pi86-validation-logs
```

Arm capture before resetting or reconnecting the RP2350 when the firmware emits one bounded report after boot.

## 6. Accept physical evidence, not appearance

A PASS should establish every applicable layer:

- RESET qualification and first post-reset fetch;
- address, cycle type, and byte lanes;
- exact V30-visible response data;
- CPU-visible control flow or computation witness;
- PIO/DMA qualified-pair completion;
- zero response-deadline miss for supported cycles;
- no unqualified drive command;
- matching transport sequence and complete publication;
- passive evidence independent of the application reply;
- terminal RESET/CLK/AD ownership.
- or, for a persistent profile, validated idle/interrupt liveness with AD high-Z.

An expected string, drained DMA counter, or active GPIO waveform is not sufficient by itself.

Raw CDC `.log` output is evidence. A host-generated `.json` result is a deterministic interpretation of that evidence. Retain both when they form part of an accepted gate.

## 7. Classify the result honestly

Every result must name its response architecture:

```text
fixed / prestaged
address-qualified
bounded memory
cached guaranteed hit
general memory with defined miss policy
integrated system
```

Do not promote a fixed-response frequency result into a general-memory or integrated-system claim.

## Known regression families

Use the relevant accepted evidence as the comparison baseline:

- [`validation/pc1b_pio_direct_frequency_sweep.md`](validation/pc1b_pio_direct_frequency_sweep.md) - fixed PIO-direct response;
- PC1-C records under [`validation/`](validation/) - address, ROM, BIOS, RAM, and byte lanes;
- [`validation/dc_a_dual_core_foundation_validation.md`](validation/dc_a_dual_core_foundation_validation.md) - dual-core isolation;
- [`validation/dc_b1a_trace_backpressure_validation.md`](validation/dc_b1a_trace_backpressure_validation.md) - nonblocking trace transport;
- [`validation/ai_b2_hid_composite_600khz_validation.md`](validation/ai_b2_hid_composite_600khz_validation.md) - HID result plus CDC evidence;
- [`validation/ai_b3_codex_initiated_greeting_validation.md`](validation/ai_b3_codex_initiated_greeting_validation.md) - first accepted Codex adapter.

## Failure handling

On failure:

1. preserve the complete raw output;
2. record firmware target, commit, UF2 hash, clock, board, CPU, and host command;
3. identify the first failed acceptance field or first trace divergence;
4. return the V30 to RESET-high, CLK-low, AD-high-Z;
5. change one primary hypothesis at a time;
6. rerun the last known-good target if hardware state is uncertain.

See [`bringup/recovery.md`](bringup/recovery.md) for USB, CDC/HID, flashing, and rollback recovery.

## Historical gates

The original functional path from GPIO mapping through clock/reset, ROM, RAM, byte lanes, I/O, PIC, multi-IRQ, and PIT is retained in [`bringup/gate_history.md`](bringup/gate_history.md). Those gates are engineering history and regression evidence, not the current implementation queue.

