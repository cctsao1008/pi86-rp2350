bits 16
cpu 8086
org 0

; Issue #112 C9V: reconstruct each 16-bit source through two explicit byte
; reads, then encode the CPU-visible AX value into a physical write address.
;
; This distinguishes an aligned WORD read fault from a general host->CPU data
; mapping fault.  No stack, CALL/RET, or write-data witness is used.

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
    mov al, [cs:SRC0]
    mov ah, [cs:SRC0 + 1]
    mov bx, ax
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov al, [cs:SRC1]
    mov ah, [cs:SRC1 + 1]
    mov bx, ax
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov al, [cs:SRC2]
    mov ah, [cs:SRC2 + 1]
    mov bx, ax
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov al, [cs:SRC3]
    mov ah, [cs:SRC3 + 1]
    mov bx, ax
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
