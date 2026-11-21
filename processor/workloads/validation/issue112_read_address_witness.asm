bits 16
cpu 8086
org 0

; Issue #112 C9T: CPU-latched read value encoded into a physical write address.
;
; This avoids using the write-data bus as the witness.  After each aligned
; memory read the physical CPU copies AX to BX and performs a constant WORD
; store at CS:(BX + 2000h).  C9Q already proved the T1 address observer, so
; the resulting physical write address is an independent witness of the value
; the CPU actually latched.
;
; Expected physical witness addresses with CS=1000h:
;   AX=1000 -> 13000h
;   AX=FFFE -> 11FFEh  (16-bit effective-address wrap)
;   AX=D08E -> 1F08Eh
;   AX=A55A -> 1C55Ah

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
    mov ax, [cs:SRC0]
    mov bx, ax
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov ax, [cs:SRC1]
    mov bx, ax
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov ax, [cs:SRC2]
    mov bx, ax
    mov word [cs:bx + WITNESS_BASE], 0c9a5h

    mov ax, [cs:SRC3]
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
