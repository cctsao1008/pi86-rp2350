# Fast Inverse Square Root on Intel 8086 / NEC V30

A native fixed-point inverse-square-root workload for the physical Intel 8086 / NEC V30 execution environment used by RP86.

This example computes an approximation to

\[
\frac{1}{\sqrt{x}}
\]

without relying on an 8087 coprocessor, IEEE-754 floating point, or host-side arithmetic.

The design follows the same broad optimization philosophy that made the Quake fast inverse square root famous — obtain a cheap initial estimate and refine it numerically — but adapts the idea to the actual architecture of an 8086-class processor.

## Purpose

The workload answers a specific architecture question:

> How can an Intel 8086 / NEC V30 compute inverse square root efficiently when the processor has no built-in floating-point unit?

Rather than emulate the Quake `0x5f3759df` trick, this implementation uses:

```text
fixed-point arithmetic
        |
        v
power-of-four normalization
        |
        v
small lookup table
        |
        v
Newton-Raphson refinement
        |
        v
1 / sqrt(x)
```

The result is a native 16-bit numerical workload that is appropriate for the processor being exercised.

## Why inverse square root matters

Inverse square root appears naturally in vector normalization. For a vector

\[
\mathbf{v}=(x,y,z)
\]

its normalized form is

\[
\hat{\mathbf{v}}
=
\frac{\mathbf{v}}{\lVert\mathbf{v}\rVert}
=
\mathbf{v}\frac{1}{\sqrt{x^2+y^2+z^2}}.
\]

This makes inverse square root a useful building block for later workloads involving:

- vector normalization;
- dot products and direction vectors;
- 3D transforms;
- fixed-point lighting calculations;
- geometry and signal-processing experiments.

## Why this is not a direct Quake port

The well-known Quake fast inverse square root begins with an IEEE-754 bit-level estimate such as:

```c
i = 0x5f3759df - (i >> 1);
```

That technique depends on properties of 32-bit IEEE-754 single-precision floating-point representation.

The original Intel 8086, however:

- has no integrated floating-point unit;
- treats the 8087 as an optional coprocessor;
- is fundamentally a 16-bit integer machine;
- has no native 32-bit general-purpose registers;
- would require software floating point if IEEE-754 arithmetic were forced onto it.

For this workload, software floating point would obscure the actual processor architecture. The implementation therefore uses a fixed-point method that preserves the same high-level idea:

```text
cheap approximation + numerical refinement
```

## Number representation

The workload uses unsigned Q8.8 fixed-point values.

A Q8.8 value stores eight fractional bits:

\[
\text{real value}=\frac{\text{stored integer}}{256}.
\]

Examples:

| Real value | Q8.8 value | Hex |
| ---: | ---: | ---: |
| 0.25 | 64 | `0x0040` |
| 0.5 | 128 | `0x0080` |
| 1.0 | 256 | `0x0100` |
| 1.5 | 384 | `0x0180` |
| 2.0 | 512 | `0x0200` |
| 4.0 | 1024 | `0x0400` |

Multiplication follows the usual fixed-point rule:

\[
Q8.8 \times Q8.8 = Q16.16.
\]

The 32-bit product is formed by the 8086 `MUL` instruction in `DX:AX`, then shifted right by eight bits to return to Q8.8.

Conceptually:

```c
result = (a * b) >> 8;
```

## Algorithm

The input is normalized by powers of four so that

\[
x=m4^k,
\qquad 1\le m<4.
\]

Then

\[
\frac{1}{\sqrt{x}}
=
\frac{1}{\sqrt{m}}2^{-k}.
\]

This is useful on a binary processor because the exponent correction becomes a power-of-two shift.

The workload therefore performs:

```text
Input x (Q8.8)
     |
     v
Normalize x to m in [1, 4)
     |
     +-- remember power-of-four exponent k
     |
     v
Lookup initial estimate y0 ~= 1/sqrt(m)
     |
     v
Newton-Raphson refinement
     |
     v
Newton-Raphson refinement
     |
     v
Apply 2^-k exponent correction
     |
     v
Return Q8.8 result
```

## Newton-Raphson refinement

The refinement step is

\[
y_{n+1}
=
y_n\left(\frac{3}{2}-\frac{x}{2}y_n^2\right).
\]

In code-oriented form:

```text
y2         = y * y
xy2        = x * y2
correction = 1.5 - 0.5 * xy2
y_next     = y * correction
```

Each iteration therefore requires three fixed-point multiplications:

1. `y * y`;
2. `x * y^2`;
3. `y * correction`.

The important property is that, once the initial estimate is reasonably close, the error decreases approximately quadratically. This allows a very small lookup table to provide enough accuracy for rapid refinement.

## Lookup table

The implementation uses a 24-entry table of 16-bit Q8.8 values:

```text
24 entries x 2 bytes = 48 bytes
```

The table is intentionally small. Its role is not to produce the final result directly; it only supplies an initial estimate that is sufficiently close for Newton-Raphson to converge rapidly.

This creates a useful architecture trade-off:

```text
ROM/table size
      vs
initial approximation error
      vs
number of Newton iterations
```

The current workload chooses a small 48-byte table and two refinement iterations.

## Self-test vectors

The workload includes deterministic test inputs:

| x | Ideal `1/sqrt(x)` | Q8.8 reference |
| ---: | ---: | ---: |
| 1 | 1.000000 | 256 |
| 2 | 0.707107 | 181 |
| 4 | 0.500000 | 128 |
| 9 | 0.333333 | 85 |
| 16 | 0.250000 | 64 |

The acceptance window is:

```text
reference +/- 1 Q8.8 LSB
```

One Q8.8 LSB is

\[
\frac{1}{256}=0.00390625.
\]

The purpose of the self-test is to verify the complete fixed-point path, not to claim floating-point-level precision.

## Runtime and package model

`workload.json` declares this example as a normal RP86 workload using `clock-stepped` execution.

The build system packages it as:

```text
INVSQRT.P86W
```

The source is assembled as native 16-bit Intel 8086 / NEC V30 instructions. The RP2350 host infrastructure provides loading, clocking, packaging, and diagnostic services; the inverse-square-root algorithm itself executes on the physical 8086-class processor.

The workload uses the existing RP86 diagnostic console interface at port `0x00E9` for textual self-test output.

## Architecture assumptions

Designed for:

- Intel 8086;
- NEC V30;
- 16-bit native execution;
- integer `MUL` and shift operations;
- RP86 diagnostic/service ports.

Not required by the numerical algorithm:

- 8087;
- IEEE-754 floating point;
- 32-bit general-purpose registers;
- host-side floating-point computation.

## Engineering significance

The main point of this example is not the specific lookup table or the exact Q-format. It is the design pattern:

\[
\boxed{\text{Transform} \rightarrow \text{Approximate} \rightarrow \text{Refine}}
\]

On an 8086-class processor, the best representation of a numerical problem may be very different from the representation chosen on a later x86 CPU or GPU.

This workload is therefore both a numerical example and an architecture-awareness example: solve the required mathematical problem using operations that map naturally onto the physical processor.

## Future work

A natural follow-on sequence is:

```text
fast-invsqrt
     |
     v
vector-normalize
     |
     v
dot-product
     |
     v
3D transform
     |
     v
fixed-point lighting
```

Possible benchmark extensions include comparing:

- lookup-only approximation;
- lookup + one Newton iteration;
- lookup + two Newton iterations;
- integer square root + integer division;
- Intel 8086 versus NEC V30 execution behavior and timing.

## Validation status

The source and package structure follow the existing RP86 workload conventions. Build output and physical-processor execution should be treated as the final validation authority.

When physical execution results are available, this README can be extended with measured output, instruction/clock cost, and Intel 8086 versus NEC V30 comparison data.
