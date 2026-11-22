bits 16
cpu 8086
org 0

; Continuous stackless PI workload for physical Intel 8086 / NEC V30.
;
; Project goal:
;   keep the physical processor doing real arithmetic continuously, without
;   depending on stack traffic, CALL/RET, or scratch-memory reads.
;
; Method:
;   Machin formula
;       pi = 16*atan(1/5) - 4*atan(1/239)
;
;   Each arctan is evaluated in unsigned Q3.13 using the Gregory series:
;       atan(1/x) = 1/x - 1/(3*x^3) + 1/(5*x^5) - ...
;
;   Q3.13 scale = 8192. The resulting approximation is:
;       25736 / 8192 = 3.1416015625
;
;   Before publication the 16-bit Q3.13 result is shifted left by 11 to the
;   existing Q8.24 telemetry format, so the RP2350-side telemetry reader and
;   expected-value fields remain unchanged.
;
; Deliberate constraints:
;   - no PUSH/POP
;   - no CALL/RET
;   - no stack accesses
;   - no scratch-memory reads
;   - no data-memory reads at all
;   - aligned telemetry writes only
;
; This leaves instruction fetch + integer ALU/DIV + telemetry writes as the
; sustained physical-CPU workload.

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

%define PI_Q8_24_LO       03F6Ah
%define PI_Q8_24_HI       0324h

start:
    cli
    cld

    ; Persistent completed-epoch counter lives in BP. SP is used only as a
    ; general register; no stack instruction is executed anywhere.
    xor bp, bp

    mov word [cs:T_MAGIC0], 04950h
    mov word [cs:T_MAGIC1], 03638h
    mov word [cs:T_VERSION], 2
    mov word [cs:T_SEQ], 0
    mov word [cs:T_EPOCH_LO], 0
    mov word [cs:T_EPOCH_HI], 0
    mov word [cs:T_PI_LO], 0
    mov word [cs:T_PI_HI], 0
    mov word [cs:T_EXPECTED_LO], PI_Q8_24_LO
    mov word [cs:T_EXPECTED_HI], PI_Q8_24_HI
    mov word [cs:T_DELTA_LO], 0
    mov word [cs:T_DELTA_HI], 0
    mov word [cs:T_STATE], STATE_RUNNING

.forever:
    ; odd seq = computation in progress
    mov cx, bp
    shl cx, 1
    inc cx
    mov [cs:T_SEQ], cx
    mov word [cs:T_STATE], STATE_RUNNING

    ; ---------------------------------------------------------------
    ; atan(1/5), Q3.13
    ; BX = x^2 = 25
    ; SI = current power
    ; DI = accumulated sum
    ; CX = odd denominator; bit1 selects add/subtract
    ; ---------------------------------------------------------------
    mov ax, 8192
    xor dx, dx
    mov bx, 5
    div bx                  ; AX = floor(8192/5)
    mov si, ax
    xor di, di
    mov bx, 25
    mov cx, 1

.atan5:
    mov ax, si
    xor dx, dx
    div cx                  ; AX = power / odd
    or ax, ax
    jz .atan5_done

    test cx, 2
    jnz .atan5_sub
    add di, ax
    jmp short .atan5_advance
.atan5_sub:
    sub di, ax
.atan5_advance:
    mov ax, si
    xor dx, dx
    div bx                  ; power /= 25
    mov si, ax
    add cx, 2
    jmp .atan5

.atan5_done:
    ; save 16*atan(1/5) in SP (SP is just a general register here)
    mov sp, di
    shl sp, 1
    shl sp, 1
    shl sp, 1
    shl sp, 1

    ; ---------------------------------------------------------------
    ; atan(1/239), Q3.13
    ; ---------------------------------------------------------------
    mov ax, 8192
    xor dx, dx
    mov bx, 239
    div bx
    mov si, ax
    xor di, di
    mov bx, 57121           ; 239*239
    mov cx, 1

.atan239:
    mov ax, si
    xor dx, dx
    div cx
    or ax, ax
    jz .atan239_done

    test cx, 2
    jnz .atan239_sub
    add di, ax
    jmp short .atan239_advance
.atan239_sub:
    sub di, ax
.atan239_advance:
    mov ax, si
    xor dx, dx
    div bx
    mov si, ax
    add cx, 2
    jmp .atan239

.atan239_done:
    ; SP = 16*atan(1/5) - 4*atan(1/239) in Q3.13
    shl di, 1
    shl di, 1
    sub sp, di

    ; Convert Q3.13 -> Q8.24 by << 11, publish DX:AX.
    mov ax, sp
    xor dx, dx
    mov cx, 11
.to_q8_24:
    shl ax, 1
    rcl dx, 1
    loop .to_q8_24

    mov [cs:T_PI_LO], ax
    mov [cs:T_PI_HI], dx

    ; signed 32-bit delta against floor(pi * 2^24)
    sub ax, PI_Q8_24_LO
    sbb dx, PI_Q8_24_HI
    mov [cs:T_DELTA_LO], ax
    mov [cs:T_DELTA_HI], dx

    inc bp
    mov [cs:T_EPOCH_LO], bp
    mov word [cs:T_EPOCH_HI], 0
    mov word [cs:T_STATE], STATE_PUBLISHED

    ; even seq = stable publication
    mov cx, bp
    shl cx, 1
    mov [cs:T_SEQ], cx

    jmp .forever

times TELEMETRY_OFFSET - ($ - $$) db 0
telemetry:
    times 32 db 0
