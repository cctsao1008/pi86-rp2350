bits 16
org 0

; Minimal persistent workload for canonical load/run/status/stop/restart.
; It emits one line and one numeric result, then remains in a two-byte native
; loop so the clock-stepped controller continues serving observable fetches.

DIAGNOSTIC_PORT equ 00E9h
RESULT_PORT     equ 00E8h

%macro putc 1
    mov al, %1
    out dx, al
%endmacro

start:
    cli
    mov dx, DIAGNOSTIC_PORT
    putc 'R'
    putc 'P'
    putc '8'
    putc '6'
    putc ' '
    putc 'R'
    putc 'U'
    putc 'N'
    putc 13
    putc 10

    mov ax, 19
    add ax, 23
    mov dx, RESULT_PORT
    out dx, ax

idle:
    jmp short idle
