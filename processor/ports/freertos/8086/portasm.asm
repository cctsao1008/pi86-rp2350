bits 16

%include "rp86_abi.inc"

; Open Watcom small-model OMF segment/group declarations.
segment CONST public align=16 class=DATA use16
segment CONST2 public align=16 class=DATA use16
segment _DATA public align=16 class=DATA use16
segment DATA public align=16 class=DATA use16
segment _BSS public align=16 class=BSS use16

; One-shot #60 physical trace state.  Startup clears DGROUP BSS before C runs.
rp86_tick_trace_state:  resw 1
rp86_yield_trace_state: resw 1

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

%macro RP86_TRACE_WORD 1
    mov dx, RP86_IO_PORT_RESULT
    mov ax, %1
    out dx, ax
%endmacro

%macro RP86_TRACE_WORD_PRESERVE 1
    push ax
    push dx
    RP86_TRACE_WORD %1
    pop dx
    pop ax
%endmacro

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
    push dx
    push es
    RP86_TRACE_WORD 0x6200

    xor ax, ax
    mov es, ax

    mov ax, rp86_freertos_tick_isr
    mov [es:RP86_IVT_TICK_OFFSET_ADDRESS], ax
    push cs
    pop ax
    mov [es:RP86_IVT_TICK_SEGMENT_ADDRESS], ax

    mov ax, rp86_freertos_yield_isr
    mov [es:RP86_FREERTOS_YIELD_IVT], ax
    push cs
    pop ax
    mov [es:(RP86_FREERTOS_YIELD_IVT + 2)], ax

    RP86_TRACE_WORD 0x6201
    pop es
    pop dx
    pop ax
    ret

; Load the first TCB's near pxTopOfStack.  The v1 port deliberately uses one
; common stack segment (DGROUP) so FreeRTOS's 16-bit near stack pointer remains
; the actual SP value.  MOV SS is immediately followed by MOV SP while IRQs are
; disabled; IRET restores the task's IF state from the fabricated frame.
_rp86PortStartFirstTask:
    cli
    RP86_TRACE_WORD_PRESERVE 0x6202

    mov ax, seg _pxCurrentTCB
    mov ds, ax
    mov es, ax
    mov bx, [_pxCurrentTCB]
    mov dx, [bx]
    RP86_TRACE_WORD_PRESERVE 0x6203

    mov ss, ax
    mov sp, dx

    ; The selected task stack is active now.  AX/DX may be clobbered because
    ; the fabricated task context below restores both before the first IRET.
    RP86_TRACE_WORD 0x6204

    pop bp
    pop di
    pop si
    pop ds
    pop es
    pop dx
    pop cx
    pop bx
    pop ax

    ; Preserve the fully restored register image while proving that the initial
    ; frame was consumed successfully and IRET is the next instruction.
    RP86_TRACE_WORD_PRESERVE 0x6205
    iret

; Physical RP2350-owned 100 Hz tick, vector 21h.
rp86_freertos_tick_isr:
    RP86_SAVE_CONTEXT
    cld

    mov ax, seg _pxCurrentTCB
    mov ds, ax
    mov es, ax

    cmp word [rp86_tick_trace_state], 0
    jne .tick_trace_entered
    mov word [rp86_tick_trace_state], 1
    RP86_TRACE_WORD_PRESERVE 0x6210
.tick_trace_entered:

    call _xTaskIncrementTick

    cmp word [rp86_tick_trace_state], 1
    jne .tick_return_checked
    RP86_TRACE_WORD_PRESERVE 0x6211
.tick_return_checked:

    or ax, ax
    jz .tick_no_switch

    cmp word [rp86_tick_trace_state], 1
    jne .tick_switch_trace_done
    RP86_TRACE_WORD_PRESERVE 0x6212
.tick_switch_trace_done:

    mov bx, [_pxCurrentTCB]
    mov [bx], sp
    call _vTaskSwitchContext
    mov bx, [_pxCurrentTCB]
    mov sp, [bx]

    cmp word [rp86_tick_trace_state], 1
    jne .tick_eoi
    mov word [rp86_tick_trace_state], 2
    ; Selected task stack is active.  Do not push/pop here; the following
    ; restore discards the temporary AX/DX values from this marker.
    RP86_TRACE_WORD 0x6213
    jmp short .tick_eoi

.tick_no_switch:
    cmp word [rp86_tick_trace_state], 1
    jne .tick_eoi
    mov word [rp86_tick_trace_state], 2
    RP86_TRACE_WORD 0x6214

.tick_eoi:
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

    cmp word [rp86_yield_trace_state], 0
    jne .yield_trace_entered
    mov word [rp86_yield_trace_state], 1
    RP86_TRACE_WORD_PRESERVE 0x6220
.yield_trace_entered:

    mov bx, [_pxCurrentTCB]
    mov [bx], sp
    call _vTaskSwitchContext
    mov bx, [_pxCurrentTCB]
    mov sp, [bx]

    cmp word [rp86_yield_trace_state], 1
    jne .yield_restore
    mov word [rp86_yield_trace_state], 2
    ; Selected task stack is active; restoration below replaces AX/DX.
    RP86_TRACE_WORD 0x6221

.yield_restore:
    jmp rp86_restore_context
