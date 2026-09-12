bits 16

%include "rp86_abi.inc"

segment CONST public align=16 class=DATA use16
segment CONST2 public align=16 class=DATA use16
segment _DATA public align=16 class=DATA use16
segment DATA public align=16 class=DATA use16
segment _BSS public align=16 class=BSS use16

; Keep the validated FreeRTOS v1 small-model invariant: all kernel/task C
; execution uses SS=DS=DGROUP, including the pre-scheduler bootstrap frame.
rp86_freertos_bootstrap_stack:
    resw 256
rp86_freertos_bootstrap_stack_top:

group DGROUP CONST CONST2 _DATA DATA _BSS

segment _TEXT public align=16 class=CODE use16

global rp86_c16_entry
global _rp86SystemFail

extern _rp86_freertos_system_main
extern _rp86_data_anchor
extern _edata
extern _end

%macro putc 1
    mov al, %1
    out dx, al
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

    mov ax, ds
    mov ss, ax
    mov sp, rp86_freertos_bootstrap_stack_top

    call _rp86_freertos_system_main
    ; A healthy system workload never returns from the scheduler. AX is reason.
    jmp short rp86_terminal_fail_ax

; Near __cdecl.  This path is terminal, so BP does not need to be restored.
_rp86SystemFail:
    mov bp, sp
    mov ax, [ss:bp + 2]

rp86_terminal_fail_ax:
    cli
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    report_fail

    mov dx, RP86_IO_PORT_CONTROL
    mov ax, RP86_CONTROL_IDLE_PREPARE
    out dx, ax

.halted:
    hlt
    jmp short .halted
