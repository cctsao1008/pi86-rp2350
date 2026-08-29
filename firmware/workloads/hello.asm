bits 16
org 0

; Canonical native hello workload with a physical-processor identity witness.
;
; D5 ib is the undocumented generalized AAD form on Intel 8086/8088:
; AL <- AL + AH * ib.  NEC V20/V30 retain their documented decimal behavior
; and ignore a non-0Ah base byte.  With AX=0102h and ib=10h this produces:
;
;   AL=12h  Intel 8086/8088 behavior
;   AL=0Ch  NEC V20/V30 behavior
;
; The HAT's 16-bit physical bus and Host declaration narrow those families to
; Intel 8086 and NEC V30.  Port 00E9h is the project's early native diagnostic
; console; the Host treats this result as execution evidence, not CPUID.

%define DIAGNOSTIC_PORT 0x00E9
%define CONTROL_PORT 0x00E6
%define CONTROL_IDLE_PREPARE 0x0001

%macro putc 1
    mov al, %1
    out dx, al
%endmacro

%macro puts_intel 0
    putc 'H'
    putc 'E'
    putc 'L'
    putc 'L'
    putc 'O'
    putc ' '
    putc 'I'
    putc 'N'
    putc 'T'
    putc 'E'
    putc 'L'
    putc ' '
    putc '8'
    putc '0'
    putc '8'
    putc '6'
%endmacro

%macro puts_nec 0
    putc 'H'
    putc 'E'
    putc 'L'
    putc 'L'
    putc 'O'
    putc ' '
    putc 'N'
    putc 'E'
    putc 'C'
    putc ' '
    putc 'V'
    putc '3'
    putc '0'
%endmacro

%macro puts_unknown 0
    putc 'H'
    putc 'E'
    putc 'L'
    putc 'L'
    putc 'O'
    putc ' '
    putc 'U'
    putc 'N'
    putc 'K'
    putc 'N'
    putc 'O'
    putc 'W'
    putc 'N'
%endmacro

hello_entry:
    cli
    cld
    mov dx, DIAGNOSTIC_PORT
    mov ax, 0x0102
    db 0xD5, 0x10              ; AAD 16, deliberately hand encoded
    cmp al, 0x12
    je intel_8086
    cmp al, 0x0C
    je nec_v30
    puts_unknown
    jmp short done

intel_8086:
    puts_intel
    jmp short done

nec_v30:
    puts_nec

done:
    putc 13
    putc 10
    mov dx, CONTROL_PORT
    mov ax, CONTROL_IDLE_PREPARE
    out dx, ax
halted:
    hlt
    jmp short halted
