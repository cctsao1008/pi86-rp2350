# Physical Intel 8086 pi-spigot — Stage 1

This is a **bounded first-stage** bare-metal Intel 8086 NASM workload for
calculating and publishing 100 new decimal places of pi. It does not change
the existing 2 MHz continuous ROM-only pi timing target.

## Algorithm and interface

- Rabinowitz-Wagon spigot with 112 guarded iterations and 374 16-bit words
  (748 bytes) for the mutable mixed-radix array.
- All calculations, including carries and predigit confirmation, are made by
  the physical Intel 8086; RP2350 must not calculate the digits.
- Intel 8086 word I/O on port 0080h carries ASCII in its low byte and zero in
  its high byte. The stream is: integer 3, dot, exactly 100 fractional
  characters, CR, LF.
- Port 0082h word 0100h means 101 digit characters have been emitted;
  word 00EEh means the iteration guard was exhausted. Neither status alone
  proves numerical correctness.

Expected output:

    3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679

## Prerequisites / verification gates

1. Physical Intel 8086 ROM fetch must be reliable at the chosen clock.
2. CPU-writable and readable RAM must work for the 748-byte array, state,
   calls, returns and stack. The current ROM-only pi lab does not meet this.
3. RP2350 must capture each 16-bit port-0080h write without loss and report
   the port-0082h completion marker; USB/Host latency cannot stall READY.
4. Host must validate *all* 102 stream characters against an independent
   reference before PHYSICAL PASS.
5. Run the arithmetic reference:
   py -m unittest tests.build.test_pi_spigot_8086 -v

Source and oracle validation do not imply the NASM binary was built or that
the physical Intel 8086 executed it. BUILD/PHYSICAL remain pending.

## Later unbounded stage

Use an extendable unbounded spigot algorithm, arbitrary-precision integer
state, expandable storage and an output-backpressure policy. A finite
Rabinowitz-Wagon array with an infinite outer loop is NOT unbounded.
