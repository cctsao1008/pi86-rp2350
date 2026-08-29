# Physical Bring-Up and Validation

This is the current operator entry point for the RP86 hardware/runtime.

## Before power

Read [`hardware.md`](hardware.md) and [`architecture.md`](architecture.md).
The Raspberry Pi physical header position is the hardware ABI; Raspberry Pi
BCM numbers and RP2350 GPIO numbers are not interchangeable.

With power removed:

1. verify HAT orientation and pin 1;
2. inspect bent or offset pins and possible shorts;
3. confirm the installed Intel 8086 or NEC V30;
4. remove unrelated GPIO fixtures;
5. connect only the interfaces required by the run.

## Build

```bash
cd ~/github/pi86-rp2350
git pull origin main
git submodule update --init --recursive
./scripts/bootstrap_tools.sh
./scripts/build.sh --target rp86_rp2350
```

The canonical UF2 is `build/firmware/rp86_rp2350.uf2`. See
[`development/build_and_toolchain.md`](development/build_and_toolchain.md).

## Flash

Enter RP2350 ROM boot mode using either the board controls or:

```powershell
py tools\rp86.py --bootloader --timeout 5
```

Copy the canonical UF2 to the enumerated RP2350 drive and wait for USB to
re-enumerate.

## Observe

```powershell
py tools\rp86.py --interactive --heartbeat --attach `
  --display status --interval 1.0 `
  --output-dir D:\pi86-validation-logs
```

The default Host behavior automatically identifies Intel 8086 or NEC V30 from
the native processor witness.

## Acceptance boundary

A physical PASS must agree across every applicable layer:

- reset handoff and native instruction fetch;
- processor identity and native computation;
- address, cycle type, byte lanes, and response data;
- PIO/DMA completion and qualified bus ownership;
- INTR and both INTA cycles when interrupts are exercised;
- Host result and retained CDC evidence;
- safe idle or terminal electrical state.

The persistent runtime normally remains in native `STI`/`HLT` idle with the AD
bus high-Z between serviced cycles. A bounded diagnostic may instead finish at
RESET high, CLK low, and AD high-Z.

Current accepted evidence is indexed by [`docs/README.md`](README.md). Git
history preserves the completed Gate and characterization experiments; they
are not part of the current source tree.

## Failure handling

1. preserve the raw output and session JSON;
2. record the commit, UF2 hash, processor, clock, and Host command;
3. identify the first failed field or trace divergence;
4. return the bus to a safe state if execution is no longer controlled;
5. change one primary hypothesis at a time.

See [`bringup/recovery.md`](bringup/recovery.md) for USB, flashing, and runtime
recovery.
