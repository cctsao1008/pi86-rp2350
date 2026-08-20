bits 16
org 0

; PC1-C0C1-B2-B same-run one-slot RAM proof.
;
; Control flow is deliberately independent of the value read from 0100h.
; Epoch A can therefore run with RAM cycles high-Z and teach the exact ROM
; fetch order.  Epoch B must capture 1234h from the write cycle, replay it on
; the later read cycle, and expose the result with a second write to 0102h.

start:
    cli
    mov ax, 0x1234
    mov [0x0100], ax
    mov bx, [0x0100]
    mov [0x0102], bx

checkpoint:
    jmp checkpoint
    nop                         ; unreachable padding keeps ROM word-complete
