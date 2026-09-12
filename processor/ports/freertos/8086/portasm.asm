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

; RAM-backed first-yield localization witness.  This intentionally lives in
; the port rather than the validation workload so the scheduler boundary can
; be observed without coupling the portable layer to application telemetry.
;
;   +00 stage      0 idle, 1 ISR entered, 2 current saved,
;                  3 scheduler returned, 4 next SP loaded, 5 pre-IRET
;   +02 old_ss
;   +04 old_sp      saved-context SP written to the outgoing TCB
;   +06 old_tcb
;   +08 new_ss      current SS after selecting the incoming task
;   +0A new_sp      saved-context SP loaded from the incoming TCB
;   +0C new_tcb
;
; The current port deliberately uses one DGROUP/SS stack domain for every
; FreeRTOS task, so SS is not switched per task.  Capturing both values makes
; that invariant explicit in physical-hardware evidence.
global _gRp86PortTrace
_gRp86PortTrace:
    resw 7

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

%define RP86_PORT_TRACE_STAGE       0
%define RP86_PORT_TRACE_OLD_SS      2
%define RP86_PORT_TRACE_OLD_SP      4
%define RP86_PORT_TRACE_OLD_TCB     6
%define RP86_PORT_TRACE_NEW_SS      8
%define RP86_PORT_TRACE_NEW_SP      10
%define RP86_PORT_TRACE_NEW_TCB     12

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
    jne .tick_restore
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
    jmp short .tick_restore

.tick_no_switch:
    cmp word [rp86_tick_trace_state], 1
    jne .tick_restore
    mov word [rp86_tick_trace_state], 4
    RP86_TRACE_WORD 0x6214

.tick_restore:
    ; Keep the external tick in service through the common restore path.  The
    ; PIC EOI is emitted only after the selected task registers are restored,
    ; so the RP2350 post-EOI recovery window protects foreground execution.
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
    jne .restore_yield_trace
    mov word [rp86_tick_trace_state], 5
    mov [rp86_trace_saved_ax], ax
    mov [rp86_trace_saved_dx], dx
    mov dx, RP86_IO_PORT_RESULT
    mov ax, 0x6236
    out dx, ax
    mov ax, [rp86_trace_saved_ax]
    mov dx, [rp86_trace_saved_dx]

.restore_yield_trace:
    ; If this restore belongs to the first voluntary yield, mark the exact
    ; pre-IRET boundary without perturbing the selected task's register image.
    push ax
    push ds
    mov ax, seg _gRp86PortTrace
    mov ds, ax
    cmp word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 4
    jne .restore_yield_trace_done
    mov word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 5
.restore_yield_trace_done:
    pop ds
    pop ax

.restore_eoi:
    ; Every scheduler restore emits the same PIC command immediately before
    ; IRET.  For a hardware tick it is the real EOI.  For an INT 80h voluntary
    ; yield no tick is in service, so RP2350 treats the write as a scheduler
    ; resume fence: it refreshes the processor-progress gate and may retract an
    ; already-asserted but not-yet-accepted pending INTR, preserving the request
    ; for reassertion after the newly selected task gets foreground progress.
    push ax
    mov al, RP86_PIC_COMMAND_EOI
    out RP86_IO_PORT_PIC_COMMAND, al
    pop ax
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

    ; Capture the first real task-to-task switch in RAM.  Unlike the result-port
    ; markers this remains readable after execution stalls and gives the Host
    ; enough information to distinguish blocking, scheduling, and restore.
    cmp word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 0
    jne .yield_ram_trace_skip_enter
    mov word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 1
    mov ax, ss
    mov [_gRp86PortTrace + RP86_PORT_TRACE_OLD_SS], ax
    mov [_gRp86PortTrace + RP86_PORT_TRACE_OLD_SP], sp
    mov ax, [_pxCurrentTCB]
    mov [_gRp86PortTrace + RP86_PORT_TRACE_OLD_TCB], ax
.yield_ram_trace_skip_enter:

    mov bx, [_pxCurrentTCB]
    mov [bx], sp

    cmp word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 1
    jne .yield_ram_trace_saved
    mov word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 2
.yield_ram_trace_saved:

    call _vTaskSwitchContext

    cmp word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 2
    jne .yield_ram_trace_scheduled
    mov word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 3
.yield_ram_trace_scheduled:

    mov bx, [_pxCurrentTCB]
    mov dx, [bx]

    cmp word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 3
    jne .yield_ram_trace_selected
    mov ax, ss
    mov [_gRp86PortTrace + RP86_PORT_TRACE_NEW_SS], ax
    mov [_gRp86PortTrace + RP86_PORT_TRACE_NEW_SP], dx
    mov [_gRp86PortTrace + RP86_PORT_TRACE_NEW_TCB], bx
.yield_ram_trace_selected:

    mov sp, dx

    cmp word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 3
    jne .yield_ram_trace_loaded
    mov word [_gRp86PortTrace + RP86_PORT_TRACE_STAGE], 4
.yield_ram_trace_loaded:

    cmp word [rp86_yield_trace_state], 1
    jne .yield_restore
    mov word [rp86_yield_trace_state], 2
    RP86_TRACE_WORD 0x6221

.yield_restore:
    jmp rp86_restore_context
