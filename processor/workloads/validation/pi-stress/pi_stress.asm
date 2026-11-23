bits 16
cpu 8086
org 0

; Sustained Intel 8086 / NEC V30 PI computation workload.
;
; The processor continuously recomputes pi using Machin's formula:
;
;   pi = 16 * atan(1/5) - 4 * atan(1/239)
;
; with the Gregory series:
;
;   atan(1/x) = 1/x - 1/(3*x^3) + 1/(5*x^5) - ...
;
; All arithmetic is integer-only Q8.24.  No BIOS, DOS, FPU, 80186+, or Host
; callback is required.  Each completed epoch publishes a telemetry record at
; physical address 10800h (CS=DS=1000h, offset 0800h).
;
; The workload is intentionally persistent: it never halts.  It is intended
; as a long-running FREE_RUNNING processor witness and arithmetic/bus stress
; workload.

%define TELEMETRY_OFFSET 0800h

%define T_MAGIC0          (TELEMETRY_OFFSET + 0)
%define T_MAGIC1          (TELEMETRY_OFFSET + 2)
%define T_VERSION         (TELEMETRY_OFFSET + 4)
%define T_SEQ             (TELEMETRY_OFFSET + 6)
%define T_EPOCH_LO        (TELEMETRY_OFFSET + 8)
%define T_EPOCH_HI        (TELEMETRY_OFFSET + 10)
%define T_PI_LO           (TELEMETRY_OFFSET + 12)
%define T_PI_HI           (TELEMETRY_OFFSET + 14)
%define T_EXPECTED_LO     (TELEMETRY_OFFSET + 16)
%define T_EXPECTED_HI     (TELEMETRY_OFFSET + 18)
%define T_DELTA_LO        (TELEMETRY_OFFSET + 20)
%define T_DELTA_HI        (TELEMETRY_OFFSET + 22)
%define T_STATE           (TELEMETRY_OFFSET + 24)

%define STATE_RUNNING     1
%define STATE_PUBLISHED   2

; floor(pi * 2^24) = 03243F6Ah.
%define PI_Q8_24_LO       03F6Ah
%define PI_Q8_24_HI       0324h

pi_entry:
    cli
    cld
    push cs
    pop ds

    mov word [T_MAGIC0], 04950h       ; "PI"
    mov word [T_MAGIC1], 03638h       ; "86"
    mov word [T_VERSION], 1
    mov word [T_SEQ], 0
    mov word [T_EPOCH_LO], 0
    mov word [T_EPOCH_HI], 0
    mov word [T_PI_LO], 0
    mov word [T_PI_HI], 0
    mov word [T_EXPECTED_LO], PI_Q8_24_LO
    mov word [T_EXPECTED_HI], PI_Q8_24_HI
    mov word [T_DELTA_LO], 0
    mov word [T_DELTA_HI], 0
    mov word [T_STATE], STATE_RUNNING

.forever:
    ; Seqlock-style publication:
    ; odd T_SEQ  = computation/publication in progress
    ; even T_SEQ = stable telemetry snapshot
    inc word [T_SEQ]
    mov word [T_STATE], STATE_RUNNING

    call compute_pi_q8_24             ; DX:AX = Q8.24 pi estimate

    mov [T_PI_LO], ax
    mov [T_PI_HI], dx

    ; Publish signed two-word delta against floor(pi * 2^24).
    sub ax, PI_Q8_24_LO
    sbb dx, PI_Q8_24_HI
    mov [T_DELTA_LO], ax
    mov [T_DELTA_HI], dx

    ; 32-bit completed-epoch counter.
    inc word [T_EPOCH_LO]
    jnz .epoch_done
    inc word [T_EPOCH_HI]
.epoch_done:

    mov word [T_STATE], STATE_PUBLISHED
    inc word [T_SEQ]                  ; stable/even snapshot
    jmp .forever

; ---------------------------------------------------------------------------
; compute_pi_q8_24
;
; Returns:
;   DX:AX = pi in unsigned Q8.24
;
; Uses Machin's formula.  The arctan helper terminates naturally when the next
; fixed-point term truncates to zero.
; ---------------------------------------------------------------------------
compute_pi_q8_24:
    push bx
    push cx
    push si
    push di
    push bp

    mov bx, 5
    call atan_recip_q8_24             ; DX:AX = atan(1/5)

    ; *16
    mov cx, 4
.scale16:
    shl ax, 1
    rcl dx, 1
    loop .scale16
    mov [pi_tmp_lo - $$], ax
    mov [pi_tmp_hi - $$], dx

    mov bx, 239
    call atan_recip_q8_24             ; DX:AX = atan(1/239)

    ; *4
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov si, ax
    mov di, dx

    mov ax, [pi_tmp_lo - $$]
    mov dx, [pi_tmp_hi - $$]
    sub ax, si
    sbb dx, di

    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; ---------------------------------------------------------------------------
; atan_recip_q8_24
;
; Input:
;   BX = x, x > 1
;
; Output:
;   DX:AX = atan(1/x) in unsigned Q8.24
;
; Scratch state is local to this single-threaded workload.
; ---------------------------------------------------------------------------
atan_recip_q8_24:
    push bx
    push cx
    push si
    push di
    push bp

    ; x^2 fits in 16 bits for x = 5 and x = 239.
    mov ax, bx
    mul bx
    mov [atan_square - $$], ax

    ; power = 1.0 / x, where Q8.24 1.0 = 01000000h.
    xor ax, ax
    mov dx, 0100h
    call udiv32_u16
    mov [atan_power_lo - $$], ax
    mov [atan_power_hi - $$], dx

    mov word [atan_sum_lo - $$], 0
    mov word [atan_sum_hi - $$], 0
    mov word [atan_odd - $$], 1
    mov word [atan_negative - $$], 0

.series:
    ; term = power / odd
    mov ax, [atan_power_lo - $$]
    mov dx, [atan_power_hi - $$]
    mov bx, [atan_odd - $$]
    call udiv32_u16

    mov cx, ax
    or cx, dx
    jz .done

    cmp word [atan_negative - $$], 0
    jne .subtract

    add [atan_sum_lo - $$], ax
    adc [atan_sum_hi - $$], dx
    jmp short .advance

.subtract:
    sub [atan_sum_lo - $$], ax
    sbb [atan_sum_hi - $$], dx

.advance:
    ; power /= x^2
    mov ax, [atan_power_lo - $$]
    mov dx, [atan_power_hi - $$]
    mov bx, [atan_square - $$]
    call udiv32_u16
    mov [atan_power_lo - $$], ax
    mov [atan_power_hi - $$], dx

    add word [atan_odd - $$], 2
    xor word [atan_negative - $$], 1
    jmp .series

.done:
    mov ax, [atan_sum_lo - $$]
    mov dx, [atan_sum_hi - $$]

    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; ---------------------------------------------------------------------------
; udiv32_u16
;
; Unsigned 32/16 division implemented using two legal 8086 DIV operations.
;
; Input:
;   DX:AX = 32-bit dividend
;   BX    = 16-bit nonzero divisor
;
; Output:
;   DX:AX = 32-bit quotient
;
; The first DIV produces the high quotient word and a remainder.  The second
; divides remainder:lowword, which guarantees a 16-bit quotient.
; ---------------------------------------------------------------------------
udiv32_u16:
    push cx
    push si
    push di

    mov si, ax                       ; original low word
    mov ax, dx                       ; original high word
    xor dx, dx
    div bx                           ; AX=q_hi, DX=remainder
    mov di, ax

    mov ax, si                       ; DX:AX = remainder:low
    div bx                           ; AX=q_lo
    mov cx, ax
    mov dx, di
    mov ax, cx

    pop di
    pop si
    pop cx
    ret

align 2, db 0
pi_tmp_lo      dw 0
pi_tmp_hi      dw 0
atan_power_lo  dw 0
atan_power_hi  dw 0
atan_sum_lo    dw 0
atan_sum_hi    dw 0
atan_square    dw 0
atan_odd       dw 0
atan_negative  dw 0

; Fixed telemetry page.  Host/RP2350 can inspect physical 10800h without
; requiring symbol resolution.
times TELEMETRY_OFFSET - ($ - $$) db 0

telemetry:
    times 32 db 0
