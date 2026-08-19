bits 16
org 0

; Descriptor-fed Native BIOS smoke test, mapped at physical F0000h.
;
; Keep instruction fetches strictly forward until the final checkpoint so the
; bounded PC1-C0C0 matcher stream can prove CPU execution without claiming the
; arbitrary-address PC1-C0C1 milestone. Each OUT is deliberately unrolled.

start:
    cli
    mov dx, 0x00e9

%macro diagnostic_char 1
    mov al, %1
    out dx, al
%endmacro

    diagnostic_char 'H'
    diagnostic_char 'E'
    diagnostic_char 'L'
    diagnostic_char 'L'
    diagnostic_char 'O'
    diagnostic_char ' '
    diagnostic_char 'R'
    diagnostic_char 'P'
    diagnostic_char '2'
    diagnostic_char '3'
    diagnostic_char '5'
    diagnostic_char '0'
    diagnostic_char 13
    diagnostic_char 10

checkpoint:
    jmp short checkpoint

align 2, db 0x90
