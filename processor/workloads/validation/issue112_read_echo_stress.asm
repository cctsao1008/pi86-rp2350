bits 16
cpu 8086
org 0

; Issue #112 C9R: repeated aligned read -> CPU echo discriminator.
; No stack, CALL, PUSH, POP, SS or SP dependency is used.
;
; Four immutable CS-relative WORD operands are read repeatedly.  Each value is
; immediately written to a fixed echo address so the RP2350 trace records the
; value actually latched by the physical CPU, not merely the value visible on
; the external bus later in the cycle.

%define ITERATIONS 8

%define SRC0 0100h
%define SRC1 0102h
%define SRC2 0104h
%define SRC3 0106h

%define ECHO0 0900h
%define ECHO1 0902h
%define ECHO2 0904h
%define ECHO3 0906h
%define DONE  0908h

start:
    cli
    mov cx, ITERATIONS

.repeat:
    mov ax, [cs:SRC0]
    mov [cs:ECHO0], ax

    mov ax, [cs:SRC1]
    mov [cs:ECHO1], ax

    mov ax, [cs:SRC2]
    mov [cs:ECHO2], ax

    mov ax, [cs:SRC3]
    mov [cs:ECHO3], ax

    loop .repeat

    mov word [cs:DONE], 0c9a5h

.hold:
    jmp .hold

times 0100h - ($ - $$) db 090h

    dw 01000h
    dw 0fffeh
    dw 0d08eh
    dw 0a55ah
