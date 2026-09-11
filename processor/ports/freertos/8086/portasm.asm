bits 16

%include "rp86_abi.inc"

; Open Watcom small-model OMF segment/group declarations.
segment CONST public align=16 class=DATA use16
segment CONST2 public align=16 class=DATA use16
segment _DATA public align=16 class=DATA use16
segment DATA public align=16 class=DATA use16
segment _BSS public align=16 class=BSS use16

group DGROUP CONST CONST2 _DATA DATA _BSS

segment _TEXT public align=16 class=CODE use16

global _rp86PortGetCodeSegment
global _rp86PortGetDataSegment
global _rp86PortInstallVectors
global _rp86PortStartFirstTask

extern _pxCurrentTCB
extern _vTaskSwitchContext
extern _xTaskIncrementTick

%define RP86_FREERTOS_YIELD_VECTOR 0x80
%define RP86_FREERTOS_YIELD_IVT    (RP86_FREERTOS_YIELD_VECTOR * 4)

%macro RP86_SAVE_CONTEXT 0
    push ax
    push bx
    push cx
    push dx
    push es
    push ds
    push si
    push di
    push bp
%endmacro

%macro RP86_RESTORE_CONTEXT 0
    pop bp
    pop di
    pop si
    pop ds
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    iret
%endmacro

_rp86PortGetCodeSegment:
    mov ax, cs
    ret

_rp86PortGetDataSegment:
    mov ax, ds
    ret

_rp86PortInstallVectors:
    push ax
    push es

    xor ax, ax
    mov es, ax

    mov ax, rp86_freertos_tick_isr
    mov [es:(RP86_INTERRUPT_VECTOR_PERIODIC_TICK * 4)], ax
    push cs
    pop ax
    mov [es:(RP86_INTERRUPT_VECTOR_PERIODIC_TICK * 4 + 2)], ax

    mov ax, rp86_freertos_yield_isr
    mov [es:RP86_FREERTOS_YIELD_IVT], ax
    push cs
    pop ax
    mov [es:(RP86_FREERTOS_YIELD_IVT + 2)], ax

    pop es
    pop ax
    ret

; Load the first TCB's near pxTopOfStack.  The v1 port deliberately uses one
; common stack segment (DGROUP) so FreeRTOS's 16-bit near stack pointer remains
; the actual SP value.  MOV SS is immediately followed by MOV SP while IRQs are
; disabled; IRET restores the task's IF state from the fabricated frame.
_rp86PortStartFirstTask:
    cli
    mov ax, seg _pxCurrentTCB
    mov ds, ax
    mov es, ax
    mov bx, [_pxCurrentTCB]
    mov dx, [bx]
    mov ss, ax
    mov sp, dx
    jmp rp86_restore_context

; Physical RP2350-owned 100 Hz tick, vector 21h.
rp86_freertos_tick_isr:
    RP86_SAVE_CONTEXT
    cld

    mov ax, seg _pxCurrentTCB
    mov ds, ax
    mov es, ax

    call _xTaskIncrementTick
    or ax, ax
    jz .tick_no_switch

    mov bx, [_pxCurrentTCB]
    mov [bx], sp
    call _vTaskSwitchContext
    mov bx, [_pxCurrentTCB]
    mov sp, [bx]

.tick_no_switch:
    mov dx, RP86_IO_PORT_PIC_COMMAND
    mov al, RP86_PIC_COMMAND_EOI
    out dx, al

rp86_restore_context:
    RP86_RESTORE_CONTEXT

; Voluntary taskYIELD() path.  INT 80h creates the same FLAGS/CS/IP hardware
; frame as the physical interrupt; no RP2350 EOI is required for software INT.
rp86_freertos_yield_isr:
    RP86_SAVE_CONTEXT
    cld

    mov ax, seg _pxCurrentTCB
    mov ds, ax
    mov es, ax

    mov bx, [_pxCurrentTCB]
    mov [bx], sp
    call _vTaskSwitchContext
    mov bx, [_pxCurrentTCB]
    mov sp, [bx]

    jmp rp86_restore_context
