bits 16

%include "rp86_abi.inc"

segment CONST public align=16 class=DATA use16
segment CONST2 public align=16 class=DATA use16
segment _DATA public align=16 class=DATA use16
segment DATA public align=16 class=DATA use16
segment _BSS public align=16 class=BSS use16

rp86_tick_trace_state:  resw 1
rp86_yield_trace_state: resw 1
rp86_trace_saved_ax:    resw 1
rp86_trace_saved_dx:    resw 1

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

; Capture the value before loading the marker into AX.  This matters when the
; source value itself is AX (or a memory value first loaded into AX).
; CX may be clobbered here because every use is before the selected frame is
; restored; the real task CX is still saved on that frame.
%macro RP86_TRACE_VALUE 2
    mov cx, %2
    mov dx, RP86_IO_PORT_RESULT
    mov ax, %1
    out dx, ax
    mov ax, cx
    out dx, ax
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

    ; Snapshot the exact fabricated frame selected by vTaskStartScheduler().
    ; 6250 -> SP, 6251 -> IP, 6252 -> CS, 6253 -> FLAGS, 6254 -> SS.
    mov bp, sp
    RP86_TRACE_VALUE 0x6250, sp
    RP86_TRACE_VALUE 0x6251, [ss:bp + 18]
    RP86_TRACE_VALUE 0x6252, [ss:bp + 20]
    RP86_TRACE_VALUE 0x6253, [ss:bp + 22]
    RP86_TRACE_VALUE 0x6254, ss
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

    RP86_TRACE_WORD_PRESERVE 0x6205
    iret

rp86_freertos_tick_isr:
    RP86_SAVE_CONTEXT
    cld

    mov ax, seg _pxCurrentTCB
    mov ds, ax
    mov es, ax

    ; After the first switching IRET has completed, capture the next interrupted
    ; architectural frame.  This shows where the physical 8086 actually ran
    ; between ticks even if the task-level progress marker was never reached.
    cmp word [rp86_tick_trace_state], 5
    jne .tick_live_frame_done
    mov word [rp86_tick_trace_state], 6
    mov bp, sp
    RP86_TRACE_VALUE 0x6240, sp
    RP86_TRACE_VALUE 0x6241, [ss:bp + 18]
    RP86_TRACE_VALUE 0x6242, [ss:bp + 20]
    RP86_TRACE_VALUE 0x6243, [ss:bp + 22]
    RP86_TRACE_VALUE 0x6244, ss
.tick_live_frame_done:

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

    ; First switching tick: 6230 -> SP, 6231 -> IP, 6232 -> CS,
    ; 6233 -> FLAGS, 6234 -> SS.
    mov bp, sp
    RP86_TRACE_VALUE 0x6230, sp
    RP86_TRACE_VALUE 0x6231, [ss:bp + 18]
    RP86_TRACE_VALUE 0x6232, [ss:bp + 20]
    RP86_TRACE_VALUE 0x6233, [ss:bp + 22]
    RP86_TRACE_VALUE 0x6234, ss
    RP86_TRACE_WORD 0x6213
    jmp short .tick_eoi

.tick_no_switch:
    cmp word [rp86_tick_trace_state], 1
    jne .tick_eoi
    mov word [rp86_tick_trace_state], 4
    RP86_TRACE_WORD 0x6214

.tick_eoi:
    mov dx, RP86_IO_PORT_PIC_COMMAND
    mov al, RP86_PIC_COMMAND_EOI
    out dx, al

    cmp word [rp86_tick_trace_state], 2
    jne rp86_restore_context
    mov word [rp86_tick_trace_state], 3
    RP86_TRACE_WORD 0x6235

rp86_restore_context:
    pop bp
    pop di
    pop si
    pop ds
    pop es
    pop dx
    pop cx
    pop bx
    pop ax

    ; First switching tick only: prove the software frame was fully consumed.
    ; Advance the state before returning so later tick/yield restores do not
    ; flood the result port with 6236.
    cmp word [rp86_tick_trace_state], 3
    jne .restore_iret
    mov word [rp86_tick_trace_state], 5
    mov [rp86_trace_saved_ax], ax
    mov [rp86_trace_saved_dx], dx
    mov dx, RP86_IO_PORT_RESULT
    mov ax, 0x6236
    out dx, ax
    mov ax, [rp86_trace_saved_ax]
    mov dx, [rp86_trace_saved_dx]
.restore_iret:
    iret

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
    RP86_TRACE_WORD 0x6221

.yield_restore:
    jmp rp86_restore_context