# Fast Inverse Square Root on Intel 8086 / NEC V30

A native fixed-point inverse-square-root workload without an FPU.

## Purpose

This workload answers a narrow architectural question:

> How can a native Intel 8086 / NEC V30 compute `1/sqrt(x)` efficiently without relying on an 8087, IEEE-754 single-precision representation, or host-side floating-point assistance?

The implementation is not a direct port of the well-known Quake III fast inverse-square-root routine. Instead, it applies the same optimization philosophy to an 8086-class integer machine:

```text
cheap initial approximation
        +
Newton-Raphson refinement
```

The current implementation uses unsigned Q8.8 arithmetic, normalization, a small lookup table, and two Newton-Raphson refinement steps.

## Why inverse square root matters

Inverse square root appears naturally in vector normalization. For a vector

```text
v = (x, y, z)
```

its normalized form is

```text
v_hat = v / |v|
      = v * 1/sqrt(x^2 + y^2 + z^2)
```

That makes `1/sqrt(x)` a useful primitive for later native workloads involving:

- vector normalization;
- fixed-point 3D math;
- lighting and geometry calculations;
- control and DSP-style numerical kernels.

This workload is therefore intended as a numerical building block rather than as an isolated arithmetic trick.

## Why not `0x5f3759df`?

The classic Quake III routine uses a bit-level approximation based on the structure of a 32-bit IEEE-754 single-precision floating-point value, followed by Newton-Raphson refinement.

Conceptually, the famous operation is:

```c
i = 0x5f3759df - (i >> 1);
```

That approach assumes a floating-point representation whose exponent and mantissa layout can be reinterpreted as an integer. A native 8086 does not provide that environment by itself:

- the 8086 has no integrated floating-point unit;
- the 8087 is an optional coprocessor;
- the native datapath is 16-bit;
- software floating point would defeat the purpose of this workload.

The architecture-appropriate path used here is therefore:

```text
fixed point
    -> normalization
    -> lookup-table estimate
    -> Newton-Raphson refinement
```

The important idea carried over from the Quake routine is not the magic constant. It is the separation between a cheap approximate initial estimate and a rapidly converging numerical correction.

## Number representation

The workload uses unsigned Q8.8 fixed-point values.

```text
real value     Q8.8 integer
----------     ------------
0.25           0x0040
0.50           0x0080
1.00           0x0100
2.00           0x0200
4.00           0x0400
```

A Q8.8 value represents

```text
real = integer / 256
```

Two Q8.8 values multiply to a Q16.16 intermediate. The product is shifted right by eight bits to return to Q8.8:

```text
Q8.8 * Q8.8
      -> Q16.16
      -> >> 8
      -> Q8.8
```

On the 8086 the 16-bit `MUL` instruction naturally produces a 32-bit `DX:AX` result, which is convenient for this operation.

## Algorithm

The input is normalized by powers of four so that the reduced argument lies in a compact range:

```text
x = m * 4^k
```

with

```text
1 <= m < 4
```

Then

```text
1/sqrt(x) = (1/sqrt(m)) * 2^(-k)
```

The workload therefore performs the following steps:

```text
Input x (Q8.8)
     |
     v
Normalize by powers of four
m in [1, 4)
     |
     +---- remember scale k
     |
     v
24-entry LUT
initial estimate of 1/sqrt(m)
     |
     v
Newton-Raphson iteration #1
     |
     v
Newton-Raphson iteration #2
     |
     v
Apply power-of-two scale correction
     |
     v
Result in Q8.8
```

Normalizing by powers of four is useful because the corresponding inverse-square-root scale correction is an exact power of two, which maps naturally to integer shifts.

## Newton-Raphson refinement

For

```text
y = 1/sqrt(x)
```

a convenient Newton-Raphson iteration is

```text
y_next = y * (1.5 - 0.5 * x * y * y)
```

In Q8.8 arithmetic, `1.5` is represented by `0x0180` and `0.5` by `0x0080`.

Each refinement step is dominated by three fixed-point multiplications:

```text
y * y
x * y^2
y * correction
```

The value of Newton-Raphson here is that a moderately accurate LUT estimate can be improved quickly without requiring a large table.

## Lookup table

The current implementation uses:

```text
24 entries * 16 bits = 48 bytes
```

The LUT is not intended to provide the final answer. Its job is to produce an initial estimate close enough for the Newton iterations to converge rapidly.

This creates a useful embedded-system trade-off:

```text
more LUT space
    <-> smaller initial error
    <-> fewer refinement iterations
```

The current 48-byte table deliberately favors a small memory footprint while retaining deterministic convergence for the built-in test range.

## Self-test vectors

The workload contains deterministic reference cases:

| Input `x` | Ideal `1/sqrt(x)` | Rounded Q8.8 |
|---:|---:|---:|
| 1 | 1.000000 | 256 |
| 2 | 0.707107 | 181 |
| 4 | 0.500000 | 128 |
| 9 | 0.333333 | 85 |
| 16 | 0.250000 | 64 |

The current acceptance tolerance is:

```text
+/- 1 Q8.8 LSB
```

which corresponds to approximately:

```text
+/- 0.00390625
```

This tolerance is intentionally expressed in the workload's native numeric format rather than as a host-side floating-point percentage.

## Runtime and platform assumptions

Designed for native execution on:

- Intel 8086;
- NEC V30.

The numerical algorithm does not depend on:

- an 8087 coprocessor;
- IEEE-754 arithmetic;
- 32-bit general-purpose registers;
- host-side floating-point computation.

The RP2350 host infrastructure loads, clocks, observes, and packages the workload, but the inverse-square-root calculation itself is executed by the physical 8086-class processor.

The workload uses the project's existing diagnostic console at port `0x00E9` and requests the standard idle transition through control port `0x00E6` after the self-test completes.

## Workload metadata

`workload.json` currently declares the workload as `clock-stepped` with `stdio` capability. The generated package is built as:

```text
INVSQRT.P86W
```

Generated `.bin` and `.p86w` artifacts are build outputs and are not intended to be committed to source control.

## Expected execution behavior

The workload prints a short header, executes the fixed test vectors, reports each Q8.8 result, and terminates with either `PASS` or `FAIL` through the diagnostic console.

Exact physical-processor output and timing measurements should be treated as validation evidence and recorded only after running the workload on the target hardware.

## Design intent

The core design principle is:

```text
Transform -> Approximate -> Refine
```

Rather than implementing a general software floating-point square root and then dividing, the workload changes the representation of the problem so that most of the work can be done with operations native to the 8086: shifts, table lookup, integer multiply, addition, and subtraction.

That makes the example specifically useful for studying architecture-aware numerical design on an early 16-bit processor.

## Possible follow-on workloads

This example can serve as the first numerical primitive in a small native-computation sequence:

```text
fast-invsqrt
     ->
vector-normalize
     ->
dot-product
     ->
3D transform
     ->
fixed-point lighting
```

A separate validation or benchmark workload could later compare algorithmic cost across:

- software square root plus division;
- LUT-only approximation;
- LUT plus one Newton iteration;
- LUT plus two Newton iterations;
- Intel 8086 versus NEC V30 execution characteristics.

Those comparisons should remain separate from this example so that this directory continues to demonstrate the algorithm and interface cleanly.
