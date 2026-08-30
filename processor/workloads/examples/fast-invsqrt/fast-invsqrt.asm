bits 16
org 0

; Native 8086/NEC V30 fixed-point inverse-square-root example.
;
; Computes y ~= 1/sqrt(x) without an FPU using:
;
;   1. normalize x by powers of four into m in [1, 4)
;   2. obtain a coarse Q8.8 estimate from a 24-entry lookup table
;   3. apply two Newton-Raphson refinements
;   4. restore the power-of-two scale
;
; Newton step:
;
;   y[n+1] = y[n] * (1.5 - 0.5 * m * y[n]^2)
;
; Values are unsigned Q8.8 unless noted otherwise.  The workload runs a
; deterministic self-test and accepts +/-1 LSB error versus rounded Q8.8
; reference values.  Port 00E9h is the project diagnostic console.

%define DIAGNOSTIC_PORT      0x00E9
%define CONTROL_PORT         0x00E6
%define CONTROL_IDLE_PREPARE 0x0001

%macro putc 1
    mov al, %1
    out DIAGNOSTIC_PORT, al
%endmacro

fast_invsqrt_entry:
    cli
    cld

    ; Banner.
    putc '8'
    putc '0'
    putc '8'
    putc '6'
    putc ' '
    putc 'F'
    putc 'A'
    putc 'S'
    putc 'T'
    putc ' '
    putc 'I'
    putc 'N'
    putc 'V'
    putc 'S'
    putc 'Q'
    putc 'R'
    putc 'T'
    putc ' '
    putc 'Q'
    putc '8'
    putc '.'
    putc '8'
    call newline

    ; Use an image-relative scalar. NASM 3.02 deliberately rejects an
    ; unresolved 16-bit absolute relocation in a flat binary build.
    mov si, test_vectors - $$
    mov bp, test_vector_count
    mov dl, '1'
    xor di, di                  ; failure count

test_loop:
    ; Prefix: Tn X=
    putc 'T'
    mov al, dl
    out DIAGNOSTIC_PORT, al
    putc ' '
    putc 'X'
    putc '='

    mov ax, [si]
    call print_hex16

    putc ' '
    putc 'Y'
    putc '='

    mov ax, [si]
    call invsqrt_q8_8
    mov bx, ax                  ; BX = result
    call print_hex16

    putc ' '
    putc 'E'
    putc '='
    mov ax, [si + 2]
    call print_hex16

    ; Absolute Q8.8 error <= 1 LSB is a pass.
    mov ax, bx
    sub ax, [si + 2]
    jns .abs_ready
    neg ax
.abs_ready:
    cmp ax, 1
    ja .failed

    putc ' '
    putc 'P'
    putc 'A'
    putc 'S'
    putc 'S'
    jmp short .line_done

.failed:
    inc di
    putc ' '
    putc 'F'
    putc 'A'
    putc 'I'
    putc 'L'

.line_done:
    call newline
    add si, 4
    inc dl
    dec bp
    jnz test_loop

    ; Final summary.
    cmp di, 0
    jne workload_fail

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
    call newline
    jmp short done

workload_fail:
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
    call newline

done:
    mov dx, CONTROL_PORT
    mov ax, CONTROL_IDLE_PREPARE
    out dx, ax
halted:
    hlt
    jmp short halted

; ---------------------------------------------------------------------------
; invsqrt_q8_8
;
; Input:
;   AX = unsigned Q8.8 x, x > 0
;
; Output:
;   AX = unsigned Q8.8 approximation of 1/sqrt(x)
;
; Clobbers flags only; other general registers are preserved.
;
; Normalization uses powers of four so no sqrt(2) correction is required:
;
;   x = m * 4^k,  1 <= m < 4
;   1/sqrt(x) = (1/sqrt(m)) * 2^(-k)
; ---------------------------------------------------------------------------
invsqrt_q8_8:
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    or ax, ax
    jnz .nonzero
    mov ax, 0FFFFh              ; sentinel for invalid x = 0
    jmp .return

.nonzero:
    mov si, ax                  ; SI = normalized m in Q8.8
    xor bp, bp                  ; signed k

.norm_high:
    cmp si, 1024                ; 4.0 in Q8.8
    jb .norm_low
    shr si, 1
    shr si, 1                  ; divide m by 4
    inc bp
    jmp short .norm_high

.norm_low:
    cmp si, 256                 ; 1.0 in Q8.8
    jae .norm_done
    shl si, 1
    shl si, 1                  ; multiply m by 4
    dec bp
    jmp short .norm_low

.norm_done:
    ; 24 bins over [1,4), each 0.125 wide (32 Q8.8 counts).
    mov bx, si
    sub bx, 256
    mov cl, 5
    shr bx, cl
    shl bx, 1                  ; word table index
    mov di, [bx + invsqrt_lut - $$] ; DI = initial y estimate

    ; Two Newton-Raphson refinements.
    mov cx, 2
.newton:
    mov ax, di
    mov bx, di
    call qmul_q8_8              ; AX = y^2

    mov bx, si
    call qmul_q8_8              ; AX = m*y^2

    shr ax, 1                  ; AX = 0.5*m*y^2
    mov bx, 384                ; 1.5 in Q8.8
    sub bx, ax                 ; BX = correction term

    mov ax, di
    call qmul_q8_8              ; AX = y*correction
    mov di, ax
    loop .newton

    mov ax, di

    ; Restore scale: multiply by 2^(-k).
    cmp bp, 0
    je .return
    jl .scale_up

    mov cx, bp
    shr ax, cl
    jmp short .return

.scale_up:
    mov cx, bp
    neg cx
    shl ax, cl

.return:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; ---------------------------------------------------------------------------
; qmul_q8_8
;
; AX = Q8.8 A
; BX = Q8.8 B
; returns AX = floor((A * B) / 256)
; ---------------------------------------------------------------------------
qmul_q8_8:
    mul bx                      ; DX:AX = unsigned 16x16 product
    mov al, ah                  ; result bits 7:0  <- product bits 15:8
    mov ah, dl                  ; result bits 15:8 <- product bits 23:16
    ret

; ---------------------------------------------------------------------------
; Diagnostic formatting helpers.
; ---------------------------------------------------------------------------
newline:
    putc 13
    putc 10
    ret

print_hex16:
    push ax
    push bx
    mov bx, ax
    mov al, bh
    call print_hex8
    mov al, bl
    call print_hex8
    pop bx
    pop ax
    ret

print_hex8:
    push ax
    push bx
    mov bl, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call print_nibble
    mov al, bl
    and al, 0Fh
    call print_nibble
    pop bx
    pop ax
    ret

print_nibble:
    and al, 0Fh
    cmp al, 9
    jbe .digit
    add al, 'A' - 10
    out DIAGNOSTIC_PORT, al
    ret
.digit:
    add al, '0'
    out DIAGNOSTIC_PORT, al
    ret

; ---------------------------------------------------------------------------
; Initial estimates for 1/sqrt(m), m in [1,4), Q8.8.
; Each entry uses the midpoint of a 0.125-wide bin.
; 24 words = 48 bytes.
; ---------------------------------------------------------------------------
invsqrt_lut:
    dw 248, 235, 223, 214, 205, 197
    dw 190, 184, 178, 173, 168, 164
    dw 160, 156, 153, 149, 146, 143
    dw 141, 138, 136, 133, 131, 129

; Input x and rounded reference result, both Q8.8.
; Hex values correspond to:
;   1.0  -> 1.0000
;   2.0  -> 0.7070...
;   4.0  -> 0.5000
;   9.0  -> 0.3333...
;  16.0  -> 0.2500

test_vectors:
    dw  256, 256
    dw  512, 181
    dw 1024, 128
    dw 2304,  85
    dw 4096,  64

test_vector_count equ ($ - test_vectors) / 4
