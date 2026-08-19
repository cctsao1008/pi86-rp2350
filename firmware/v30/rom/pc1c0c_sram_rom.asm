bits 16
org 0

; PC1-C0C0 CPU-visible execution image, mapped at physical F0000h.
; The three writes provide passive bus evidence for opcode delivery,
; immediate operands, register state, and segment/address formation.

start:
    cli

    mov dx, 0xf000
    mov ds, dx

    mov ax, 0x1234
    mov bx, 0x5678
    mov cx, 0xabcd

    mov [0x0100], ax
    mov [0x0102], bx
    mov [0x0104], cx

checkpoint:
    jmp short checkpoint

align 2, db 0x90
