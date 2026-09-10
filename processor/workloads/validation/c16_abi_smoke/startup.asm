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

; WLINK creates these symbols at the beginning and end of class BSS.  They are
; offsets in DGROUP for the 16-bit small model and give the freestanding RP86
; startup an explicit, linker-derived zero-initialization range.
extern _edata
extern _end

%define EXPECTED_RESULT 0x147A

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

    ; The manifest/reset handoff already owns SS:SP.  C is compiled with -zu,
    ; so SS is allowed to differ from DGROUP.  Ordinary small-model C data
    ; still requires DS (and for the initial contract ES) to address DGROUP.
    mov ax, seg _rp86_data_anchor
    mov ds, ax
    mov es, ax

    ; Freestanding C still requires deterministic zero-initialized storage.
    ; WLINK resolves _edata/_end to the BSS class bounds.  Zero exactly that
    ; byte range before any C code executes; do not rely on SRAM/loader state.
    mov di, _edata
    mov cx, _end
    sub cx, di
    xor ax, ax
    rep stosb

    ; __cdecl small-model entry: near call, 16-bit return value in AX.
    call _rp86_c16_main

    ; Publish the exact C result through the native result port first.  Then
    ; convert the checksum contract into RP86's formal diagnostic PASS/FAIL
    ; line so --physical-regression can make the acceptance decision.
    mov dx, RP86_IO_PORT_RESULT
    out dx, ax
    cmp ax, EXPECTED_RESULT
    jne .failed

    mov dx, RP86_IO_PORT_DIAGNOSTIC
    report_pass
    jmp short .complete

.failed:
    mov dx, RP86_IO_PORT_DIAGNOSTIC
    report_fail

.complete:
    ; This is terminal workload completion, not an RTOS idle HLT.
    mov dx, RP86_IO_PORT_CONTROL
    mov ax, RP86_CONTROL_IDLE_PREPARE
    out dx, ax

.halted:
    hlt
    jmp short .halted
