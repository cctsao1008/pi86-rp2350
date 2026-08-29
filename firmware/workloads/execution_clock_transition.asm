bits 16
org 0

; Native execution-clock transition proof, loaded at physical 10000h.
;
; The workload installs an INT 60h handler in writable Internal SRAM. The
; handler publishes FREE_RUNNING through the execution-clock control port.
; RP2350 commits that complete I/O write cycle at CLK=LOW, changes clock mode,
; and lets the handler return under FREE_RUNNING. The foreground then reaches
; STI/HLT and remains interrupt-wakeable.

EXECUTION_CLOCK_PORT       equ 00EAh
EXECUTION_CLOCK_FREE       equ 0001h
RESULT_PORT                equ 00E8h
INT60_VECTOR_OFFSET        equ 0180h
INT60_VECTOR_SEGMENT       equ 0182h

start:
    cli
    xor ax, ax
    mov ds, ax
    mov word [INT60_VECTOR_OFFSET], clock_request_handler - $$
    mov word [INT60_VECTOR_SEGMENT], 1000h

    mov ax, 3000h
    mov ss, ax
    mov sp, 0FFFEh

    mov ax, 19
    add ax, 23
    mov dx, RESULT_PORT
    out dx, ax

    int 60h

    sti
    nop
    hlt

align 2, db 90h
unexpected_resume:
    jmp short unexpected_resume

align 2, db 90h
clock_request_handler:
    push ax
    push dx
    mov dx, EXECUTION_CLOCK_PORT
    mov ax, EXECUTION_CLOCK_FREE
    out dx, ax
    pop dx
    pop ax
    iret
