bits 16
cpu 8086
org 0

; Continuous stackless PI workload for physical Intel 8086 / NEC V30.
;
; Goal: keep the real processor continuously doing deterministic arithmetic
; with no dependency on stack traffic or data-memory reads/writes.
;
; Machin formula:
;   pi = 16*atan(1/5) - 4*atan(1/239)
;
; Each arctan is evaluated in unsigned Q3.13 using the Gregory series.
; The deterministic integer result is:
;
;   0x6488 = 25736 / 8192 = 3.1416015625
;
; After every completed calculation the CPU executes:
;
;   OUT DX, AX
;
; with both DX and AX holding the Q3.13 result. The RP2350 can observe the
; same computed word in the address and data phases of one I/O write.
;
; Constraints:
;   - no PUSH/POP
;   - no CALL/RET
;   - no stack accesses
;   - no data-memory reads
;   - no data-memory writes
;   - one OUT witness per completed PI calculation

%define PI_Q3_13_EXPECTED 06488h

start:
    cli
    cld
    xor bp, bp                  ; completed-epoch counter, internal only

.forever:
    ; ---------------------------------------------------------------
    ; atan(1/5), Q3.13
    ; ---------------------------------------------------------------
    mov ax, 8192
    xor dx, dx
    mov bx, 5
    div bx                      ; AX = floor(8192/5)
    mov si, ax                  ; power
    xor di, di                  ; sum
    mov bx, 25                  ; x^2
    mov cx, 1                   ; odd denominator

.atan5:
    mov ax, si
    xor dx, dx
    div cx
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
    div bx
    mov si, ax
    add cx, 2
    jmp .atan5

.atan5_done:
    ; SP is used only as a general register; no stack instruction exists.
    mov sp, di
    shl sp, 1
    shl sp, 1
    shl sp, 1
    shl sp, 1                  ; 16*atan(1/5)

    ; ---------------------------------------------------------------
    ; atan(1/239), Q3.13
    ; ---------------------------------------------------------------
    mov ax, 8192
    xor dx, dx
    mov bx, 239
    div bx
    mov si, ax
    xor di, di
    mov bx, 57121              ; 239*239
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
    shl di, 1
    shl di, 1
    sub sp, di                 ; SP = Q3.13 pi approximation

    ; Report the computed result on both phases of the same I/O write.
    mov dx, sp
    mov ax, sp
    out dx, ax

    inc bp
    jmp .forever

align 2, db 090h

; Keep the expected witness value visible in the binary/source for inspection.
expected_q3_13:
    dw PI_Q3_13_EXPECTED
