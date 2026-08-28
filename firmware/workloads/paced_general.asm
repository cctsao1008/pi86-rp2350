bits 16
org 0

; General-control-flow proof for the software-paced physical bus engine.
;
; Loaded at physical 10000h and entered at 1000:0000.  The workload uses a
; real stack at 3000:FFFE, a taken LOOP branch, writable data, byte and word
; memory operations, and word I/O result/exit publication.

; Expected result:
;   sum(1..10) = 55 (0037h)
;   counter     = 10
;   byte_value  = A5h

RESULT_PORT equ 00E8h
EXIT_PORT   equ 00E6h
SUM_OFF     equ 0100h
COUNTER_OFF equ 0102h
BYTE_OFF    equ 0104h

start:
    cli
    mov ax, 1000h
    mov ds, ax
    mov es, ax
    mov ax, 3000h
    mov ss, ax
    mov sp, 0FFFEh

    mov word [SUM_OFF], 0
    mov word [COUNTER_OFF], 0
    mov byte [BYTE_OFF], 0A5h
    mov cx, 10

.loop:
    inc word [COUNTER_OFF]
    push cx
    mov ax, [COUNTER_OFF]
    add [SUM_OFF], ax
    pop cx
    loop .loop

    cmp word [SUM_OFF], 55
    jne .fail
    cmp word [COUNTER_OFF], 10
    jne .fail
    cmp byte [BYTE_OFF], 0A5h
    jne .fail

    mov ax, [SUM_OFF]
    mov dx, RESULT_PORT
    out dx, ax
    mov ax, 600Dh
    mov dx, EXIT_PORT
    out dx, ax
    hlt

.fail:
    mov ax, 0DEADh
    mov dx, RESULT_PORT
    out dx, ax
    mov dx, EXIT_PORT
    out dx, ax
    hlt

align 2, db 90h
times SUM_OFF - ($ - $$) db 90h
dw 0
dw 0
db 0
