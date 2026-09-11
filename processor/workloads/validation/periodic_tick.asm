bits 16
org 0

%include "rp86_abi.inc"

; Physical validation workload for Issue #59.
;
; Contract exercised:
; - workload-owned IVT entry for vector 21h;
; - repeated physical INTR / two-cycle INTA delivery;
; - CLI masks delivery while the RP2350 retains one pending request;
; - STI releases the retained request;
; - IRET returns to the interrupted mainline;
; - OUT 20h,20h EOI permits later ticks;
; - terminal completion remains IDLE_PREPARE + HLT, not RTOS idle.
;
; Exact tick counts are intentionally not part of the native predicate because
; the source is wall-clock based. Firmware retained evidence must additionally
; show generated > delivered and coalesced > 0 for the masked interval.
;
; NASM 3.02 diagnoses unresolved absolute 16-bit section relocations even in
; `bin` output. All image-local absolute references below are therefore written
; as explicit offsets from the current section base (`label - $$`). DS=CS, so
; those constants are exactly the offsets required by this flat workload.

INITIAL_TICKS        equ 3
MASKED_SPIN_COUNT    equ 4096
WAIT_SPIN_COUNT      equ 0FFFFh
RESULT_PASS_VALUE    equ 05921h
RESULT_FAIL_INITIAL  equ 059F1h
RESULT_FAIL_MASKED   equ 059F2h
RESULT_FAIL_RESUME   equ 059F3h
RESULT_FAIL_RECUR    equ 059F4h

start:
    cli

    ; Native data lives with this flat workload image.
    push cs
    pop ds
    xor ax, ax
    mov [tick_count - $$], ax

    ; General workloads own the IVT. Install vector 21h at 0000:0084.
    mov es, ax
    mov ax, tick_isr - $$
    mov [es:RP86_IVT_TICK_OFFSET_ADDRESS], ax
    push cs
    pop ax
    mov [es:RP86_IVT_TICK_SEGMENT_ADDRESS], ax

    sti

    ; Establish repeated delivery before testing masking/coalescing.
    mov cx, WAIT_SPIN_COUNT
.wait_initial:
    cmp word [tick_count - $$], INITIAL_TICKS
    jae .initial_ok
    loop .wait_initial
    mov ax, RESULT_FAIL_INITIAL
    jmp fail

.initial_ok:
    ; Hold IF=0 long enough for multiple 10 ms wall-clock periods. The ISR
    ; count must remain unchanged while INTR is physically pending.
    cli
    mov ax, [tick_count - $$]
    mov cx, MASKED_SPIN_COUNT
.masked_spin:
    nop
    loop .masked_spin
    cmp [tick_count - $$], ax
    jne .masked_changed

    ; STI must release the single retained request. A later second tick proves
    ; that the ISR's EOI cleared in-service state and that IRET returned here.
    sti
    mov cx, WAIT_SPIN_COUNT
.wait_resume:
    cmp [tick_count - $$], ax
    ja .resumed
    loop .wait_resume
    mov ax, RESULT_FAIL_RESUME
    jmp fail

.resumed:
    mov ax, [tick_count - $$]
    mov cx, WAIT_SPIN_COUNT
.wait_recurrence:
    cmp [tick_count - $$], ax
    ja .pass
    loop .wait_recurrence
    mov ax, RESULT_FAIL_RECUR
    jmp fail

.masked_changed:
    mov ax, RESULT_FAIL_MASKED
    jmp fail

.pass:
    cli
    mov ax, RESULT_PASS_VALUE
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    mov si, pass_text - $$
    call put_line
    jmp terminal

fail:
    cli
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    mov si, fail_text - $$
    call put_line

terminal:
    mov dx, RP86_IO_PORT_CONTROL
    mov ax, RP86_CONTROL_IDLE_PREPARE
    out dx, ax
    hlt
    jmp $

; The ISR preserves every register it modifies. EOI is deliberately issued
; before IRET; without it, the workload cannot observe a later periodic tick.
tick_isr:
    push ax
    push dx
    push ds
    push cs
    pop ds
    inc word [tick_count - $$]
    mov dx, RP86_IO_PORT_PIC_COMMAND
    mov al, RP86_PIC_COMMAND_EOI
    out dx, al
    pop ds
    pop dx
    pop ax
    iret

put_line:
.next:
    lodsb
    test al, al
    jz .done
    out dx, al
    jmp .next
.done:
    ret

pass_text db 'RESULT: PASS', 13, 10, 0
fail_text db 'RESULT: FAIL', 13, 10, 0

tick_count dw 0
