bits 16
cpu 8086
org 0

; Issue #112 C9W: aligned WORD reads using register-indirect addressing.
; This is the final instruction-encoding discriminator before changing the
; physical WORD-read dataplane.  It avoids the A1/moffs16 AX load used by C9T.
;
; Each source is read with MOV BX,[CS:SI], then BX itself forms the witness
; address.  Expected physical witness addresses are identical to C9T.

%define ITERATIONS 8
%define SRC0 0100h
%define SRC1 0102h
%define SRC2 0104h
%define SRC3 0106h
%define WITNESS_BASE 2000h
%define DONE 0908h

start:
    cli
    mov cx, ITERATIONS

.repeat:
    mov si, SRC0
    mov bx, [cs:si]
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov si, SRC1
    mov bx, [cs:si]
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov si, SRC2
    mov bx, [cs:si]
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov si, SRC3
    mov bx, [cs:si]
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    loop .repeat

    mov word [cs:DONE], 0c9a5h

.hold:
    jmp .hold

times 0100h - ($ - $$) db 090h
    dw 01000h
    dw 0fffeh
    dw 0d08eh
    dw 0a55ah
