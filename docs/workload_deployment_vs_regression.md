# Workload Deployment vs Physical Regression

RP86 distinguishes **starting a workload** from **validating a finite workload to completion**.

These are different contracts and must not be inferred from each other.

## Persistent workload deployment

Use `--start-workload` for software that is expected to keep the physical processor running, for example FreeRTOS or another resident processor runtime.

```text
py tools/rp86.py --start-workload build-freertos-system/workloads/FREERTOS-SYSTEM.P86W
```

The command performs the canonical workload lifecycle operations:

```text
Host
  -> connect to, or start, the persistent RP86 Host owner
  -> obtain current workload status
  -> stop the current processor workload when replacement requires it
  -> upload and commit the P86W image
  -> issue workload RUN
  -> require RUNNING + processor ACTIVE
  -> return PASS
```

A successful command ends with:

```text
WORKLOAD START: PASS
Physical processor continues executing after Host command return.
```

The command returning does **not** stop the physical Intel 8086 / NEC V30 and does not mean that the processor workload has completed. The background RP86 Host owner remains available for status, memory, console, stop, restart, and workload-specific observability.

Deployment success proves that the image was accepted and the processor entered the running state. It does not by itself prove application-level health.

For the FreeRTOS system workload, validate forward progress separately:

```text
py tools/rp86_freertos_status.py \
  --map build-freertos-system/processor/generated/freertos_system_validation/freertos_system_validation.map \
  --port-trace \
  --samples 6 \
  --interval 1 \
  --verify-progress
```

Healthy persistent execution reports:

```text
SUSTAINED PROGRESS: PASS
```

## Finite physical regression

Use `--physical-regression` only for a workload whose contract requires terminal completion:

```text
py tools/rp86.py --physical-regression <workload.P86W>
```

Its lifecycle is intentionally different:

```text
load -> run -> wait for COMPLETED / FAULT / TIMEOUT -> evaluate terminal result
```

A timeout remains a regression failure because a finite validation workload was expected to terminate.

Therefore this output:

```text
PHYSICAL REGRESSION: FAIL (timeout)
```

means that the **finite regression runner** did not observe terminal completion before its deadline. It does not, by itself, mean that RP2350 stopped the processor clock.

## Rule of thumb

```text
Persistent workload:
  --start-workload -> workload-specific health/status -> optional stop/restart

Finite validation workload:
  --physical-regression -> terminal PASS/FAIL
```

This separation keeps workload deployment, processor execution, and validation semantics explicit instead of treating every native program as if it should eventually return or halt.
