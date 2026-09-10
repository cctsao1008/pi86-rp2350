bits 16

%include "rp86_abi.inc"

; OMF segment names/classes follow the Open Watcom C/16 small-model
; conventions.  PUBLIC pieces with the same names are combined by WLINK.
segment CONST public align=16 class=DATA use16
segment CONST2 public align=16 class=DATA use16
segment _DATA public align=16 class=DATA use16
segment DATA public align=16 class=DATA use16
segment _BSS public align=16 class=BSS use16

group DGROUP CONST CONST2 _DATA DATA _BSS

segment _TEXT public align=16 class=CODE use16

global rp86_c16_entry
extern _rp86_c16_main
extern _rp86_data_anchor

rp86_c16_entry:
    cli
    cld

    ; The manifest/reset handoff already owns SS:SP.  C is compiled with -zu,
    ; so SS is allowed to differ from DGROUP.  Ordinary small-model C data
    ; still requires DS (and for the initial contract ES) to address DGROUP.
    mov ax, seg _rp86_data_anchor
    mov ds, ax
    mov es, ax

    ; __cdecl small-model entry: near call, 16-bit return value in AX.
    call _rp86_c16_main

    ; Publish the checksum as structured native evidence before completion.
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax

    ; Also leave a minimal human-visible marker on the diagnostic stream.
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    mov al, 'C'
    out dx, al
    mov al, '1'
    out dx, al
    mov al, '6'
    out dx, al
    mov al, 13
    out dx, al
    mov al, 10
    out dx, al

    ; This is terminal workload completion, not an RTOS idle HLT.
    mov dx, RP86_IO_PORT_CONTROL
    mov ax, RP86_CONTROL_IDLE_PREPARE
    out dx, ax

.halted:
    hlt
    jmp short .halted
