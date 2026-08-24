# Native BIOS Diagnostic Console

## Status

`0xE9` is the project-defined early Native BIOS diagnostic output port.
`PC1-C0C0-H` physically validated the first message, `HELLO RP2350\r\n`, on
2026-08-19.

This port is a bring-up contract between V30 firmware and the RP2350 companion
chipset. It is not a claim that the current machine implements an IBM PC or
other historical debug-port standard.

## V30-visible contract

| Property | Definition |
|---|---|
| I/O port | `00E9h` |
| Direction | V30 to companion chipset |
| Access | byte write |
| Read behavior | unsupported until separately specified |
| Payload | raw 8-bit diagnostic byte |
| Text convention | ASCII-compatible text; use CR LF for a completed line |
| Flow control | none in the early bounded tests |

Typical V30 code:

```asm
mov dx, 0x00e9
mov al, 'H'
out dx, al
```

Because `00E9h` is odd, a V30 byte output uses the high byte of the 16-bit AD
bus. The qualified cycle is therefore `IOW / HIGH`, with the payload observed
in AD15:AD8. `PC1-C0C0-H` recorded `4800h` for `H`, `4500h` for `E`, and the
remaining bytes in the same lane.

## Companion-chip behavior

The current regression implementation is deliberately passive:

```text
V30 OUT 00E9h, AL
        -> PIO0 observes T1 address/control and R2 data
        -> DMA stores the raw cycle in internal SRAM
        -> post-run classifier validates port, type, lane, and byte
        -> USB CDC prints the reconstructed message and PASS/FAIL result
```

PIO1 does not drive AD during an I/O write. Diagnostic cycles do not advance
the descriptor-fed ROM matcher. The observer may later feed a live console,
but live USB service must remain outside the current V30 bus deadline.

## Validation rules

A diagnostic-message test passes only when:

- every expected byte is observed as a qualified I/O write to `00E9h`;
- the byte lane matches the odd-port high-lane rule;
- byte order and exact content match the expected message;
- ROM response deadline misses remain zero;
- unqualified AD drive commands remain zero;
- the final CPU-visible checkpoint is reached;
- the terminal state is RESET high, CLK low, and AD high-Z.

The canonical first record is
[`../../validation/pc1c0c_native_bios_hello_validation.md`](../../validation/pc1c0c_native_bios_hello_validation.md).

## Intended evolution

The `0xE9` contract is suitable for reset progress, fatal codes, Mini BIOS
messages, and early monitor output before UART, display, or PC-compatible BIOS
services exist. It must remain simple enough to preserve as a permanent
golden-HAT regression when the general C0C1 ROM service is added.
