bits 16

%include "rp86_abi.inc"

segment CONST public align=16 class=DATA use16
segment CONST2 public align=16 class=DATA use16
segment _DATA public align=16 class=DATA use16
segment DATA public align=16 class=DATA use16
segment _BSS public align=16 class=BSS use16

; FreeRTOS is compiled with -zu, so Open Watcom correctly models addresses of
; stack locals as SS-relative far pointers.  The kernel's public APIs use near
; data pointers.  The RP86 v1 port makes those conversions valid by enforcing
; SS=DS=DGROUP for *all* FreeRTOS C execution, including pre-scheduler setup.
rp86_freertos_bootstrap_stack:
    resw 256
rp86_freertos_bootstrap_stack_top:

group DGROUP CONST CONST2 _DATA DATA _BSS

segment _TEXT public align=16 class=CODE use16

global rp86_c16_entry
global _rp86ValidationPass
global _rp86ValidationFail

extern _rp86_freertos_main
extern _rp86_data_anchor
extern _edata
extern _end

%macro putc 1
    mov al, %1
    out dx, al
%endmacro

%macro report_pass 0
    putc 'R'
    putc 'E'
    putc 'S'
    putc 'U'
    putc 'L'
    putc 'T'
    putc ':'
    putc ' '
    putc 'P'
    putc 'A'
    putc 'S'
    putc 'S'
    putc 13
    putc 10
%endmacro

%macro report_fail 0
    putc 'R'
    putc 'E'
    putc 'S'
    putc 'U'
    putc 'L'
    putc 'T'
    putc ':'
    putc ' '
    putc 'F'
    putc 'A'
    putc 'I'
    putc 'L'
    putc 13
    putc 10
%endmacro

rp86_c16_entry:
    cli
    cld

    mov ax, seg _rp86_data_anchor
    mov ds, ax
    mov es, ax

    ; Clear DGROUP BSS while still using the manifest-provided bootstrap stack.
    mov di, _edata
    mov cx, _end
    sub cx, di
    xor ax, ax
    rep stosb

    ; From this point onward every FreeRTOS C frame is in DGROUP.  This is the
    ; v1 near-pointer invariant used by the unmodified upstream kernel.
    mov ax, ds
    mov ss, ax
    mov sp, rp86_freertos_bootstrap_stack_top

    call _rp86_freertos_main
    ; Returning means scheduler creation/start failed. AX carries the reason.
    jmp short rp86_terminal_fail_ax

_rp86ValidationPass:
    cli
    mov ax, 0x6060
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    report_pass
    jmp short rp86_terminal_complete

_rp86ValidationFail:
    cli
    mov bp, sp
    mov ax, [ss:bp + 2]

rp86_terminal_fail_ax:
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    report_fail

rp86_terminal_complete:
    mov dx, RP86_IO_PORT_CONTROL
    mov ax, RP86_CONTROL_IDLE_PREPARE
    out dx, ax

.halted:
    hlt
    jmp short .halted
