# Physical Bring-Up Recovery Guide

This guide covers recoverable development failures around flashing, USB enumeration, CDC/HID capture, and physical regression. It does not replace electrical inspection or the canonical hardware contract.

## First response to an unknown state

1. Stop the experiment.
2. Assert or restore V30 RESET.
3. Stop CLK low if firmware still responds.
4. Confirm that the AD bus is high-Z.
5. Preserve the current raw log before reconnecting or reflashing.
6. Record the UF2 filename, source commit, and Windows command.

If bus ownership cannot be established, remove power before touching the HAT or probes.

## RP2350 boot drive does not appear

- Disconnect unrelated USB devices that could be mistaken for the board.
- Hold the board's BOOT control while applying/resetting power according to the board procedure.
- Try a known data-capable USB cable and a direct host port.
- Remove the V30 HAT only with power off if an electrical fault or power conflict is suspected.
- Do not repeatedly copy different UF2 files without recording which image actually booted.

## Firmware boots but CDC does not appear

USB composition can change between targets. A CDC-only image and a CDC+HID composite image may enumerate differently.

1. Wait for Windows device enumeration to settle.
2. Run:

   ```powershell
   py tools\ai_bridge\physical_validator.py --list-ports
   ```

3. Do not assume the previous COM number.
4. Check Windows Device Manager for the development VID/PID documented by the target.
5. If the firmware emits output only once, arm the capture tool and then reset the RP2350.

The RP2350 firmware must not require a CDC connection before releasing or safely terminating the V30 experiment unless that dependency is the explicit test subject.

## HID appears but no CDC evidence arrives

- Confirm that the selected UF2 is the composite target, not an older HID-only or CDC-only image.
- Use `v30bridge.py --list-devices` to verify the HID interface.
- Rediscover the associated CDC COM port.
- Start the bridge with both the HID device and explicit COM port available.
- Reset after capture is armed if the report is emitted once per boot.

Application traffic belongs on HID for the accepted composite bridge. Do not type the binary request into a terminal or send it over CDC.

## CDC appears but the bridge waits indefinitely

Likely causes include:

- capture started after the one-shot firmware report;
- wrong COM port;
- HID request was not delivered;
- host and firmware use different protocol versions;
- the device re-enumerated during startup;
- V30 execution did not reach the mailbox program.

Recovery:

1. preserve the partial log;
2. stop the host command;
3. rediscover COM and HID interfaces;
4. arm the correct command;
5. reset or reconnect once;
6. inspect the first missing named acceptance field.

Increasing timeout is appropriate only when the expected operation is genuinely asynchronous. It must not hide a current-cycle response-deadline failure.

## Expected reply appears but validation fails

This is a meaningful failure. The visible reply may coexist with:

- missing reset-vector evidence;
- wrong ROM identity;
- stale or mismatched sequence;
- partial publication;
- response data mismatch;
- deadline miss;
- unqualified AD drive;
- missing terminal safe state.

Do not weaken the profile to accept the greeting. Identify and fix the first failed physical contract.

## COM port changed after flashing

Windows may allocate a new COM number when interface descriptors or USB identity change.

```powershell
py tools\ai_bridge\physical_validator.py --list-ports
```

Match the USB identity rather than choosing the lowest or most familiar COM number. Bluetooth serial ports are unrelated.

## WSL can build but cannot access the device

The supported split is intentional:

```text
WSL     build and UF2 generation
Windows USB/HID/CDC physical validation
```

Synchronize both clones through Git. Do not treat a WSL build result as physical validation.

## Roll back to a known-good image

1. Select an accepted validation record matching the hardware configuration.
2. Build or retrieve the exact corresponding target and commit.
3. Flash it using the normal bootloader procedure.
4. run its original acceptance profile without modification;
5. compare the physical result before continuing the new experiment.

Avoid destructive Git rollback. Use a separate worktree, detached build, or explicit historical commit when reproducing an old image, and return to `main` without discarding unrelated work.

## Minimum failure record

Retain:

```text
date and timezone
board and CPU identity
source commit
firmware target
UF2 SHA-256
configured V30 clock
Windows COM/HID identity
exact host command
raw CDC log
host JSON result, if produced
first failed field or trace divergence
terminal electrical state
```

This record turns a failed run into reusable engineering evidence.
