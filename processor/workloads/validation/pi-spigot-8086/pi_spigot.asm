bits 16
cpu 8086
org 0

; Bounded Rabinowitz-Wagon pi spigot on a physical Intel 8086.
; Stage 1: 100 fractional digits, NOT unbounded pi yet.
; Requires a working RAM read/write dataplane and stack.
; OUT 80h,AX: low byte ASCII, high byte zero, aligned 16-bit write.
; OUT 82h,AX: 0100h complete-length; 00EEh iteration guard exhausted.
; The host must independently verify the actual digits.

%define PI_CHAR_PORT    080h
%define PI_STATUS_PORT  082h
%define DECIMAL_DIGITS  100
%define OUTPUT_DIGITS   (DECIMAL_DIGITS + 1)
%define OUTER_LIMIT     112
%define ARRAY_LEN       ((OUTER_LIMIT * 10) / 3 + 1)

start:
    cli
    cld
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, 07000h

    mov di, array
    mov cx, ARRAY_LEN
    mov ax, 2
.init_array:
    mov [di], ax
    add di, 2
    loop .init_array

    mov word [predigit], 0
    mov word [held_nines], 0
    mov word [digits_emitted], 0
    mov word [outer_remaining], OUTER_LIMIT
    mov byte [skip_leading_zero], 1

.next_digit:
    cmp word [digits_emitted], OUTPUT_DIGITS
    jae .complete
    cmp word [outer_remaining], 0
    je .guard_exhausted

    mov word [carry_q], 0
    mov si, array + ((ARRAY_LEN - 1) * 2)
    mov cx, ARRAY_LEN

.inner:
    ; DX:AX = (10 * array[i-1]) + (carry_q * i).
    mov ax, [si]
    mov bx, 10
    mul bx
    mov di, ax
    mov bp, dx

    mov ax, [carry_q]
    mul cx
    add ax, di
    adc dx, bp

    mov bx, cx
    shl bx, 1
    dec bx
    div bx
    mov [si], dx
    mov [carry_q], ax

    sub si, 2
    loop .inner

    ; The i=1 step uses divisor 1; retain final carry modulo 10.
    mov ax, [carry_q]
    xor dx, dx
    mov bx, 10
    div bx
    mov [array], dx
    mov [candidate], ax

    cmp ax, 9
    je .hold_nine
    cmp ax, 10
    je .carry_ten

    mov ax, [predigit]
    call emit_digit
.flush_nines:
    cmp word [held_nines], 0
    je .hold_candidate
    mov al, 9
    call emit_digit
    dec word [held_nines]
    jmp .flush_nines

.hold_candidate:
    mov ax, [candidate]
    mov [predigit], ax
    jmp .advance

.hold_nine:
    inc word [held_nines]
    jmp .advance

.carry_ten:
    mov ax, [predigit]
    inc ax
    call emit_digit
.flush_zeros:
    cmp word [held_nines], 0
    je .carry_done
    xor ax, ax
    call emit_digit
    dec word [held_nines]
    jmp .flush_zeros
.carry_done:
    mov word [predigit], 0

.advance:
    dec word [outer_remaining]
    jmp .next_digit

.complete:
    mov ax, 13
    out PI_CHAR_PORT, ax
    mov ax, 10
    out PI_CHAR_PORT, ax
    mov ax, 0100h
    out PI_STATUS_PORT, ax
.stop:
    hlt
    jmp short .stop

.guard_exhausted:
    mov ax, 00eeh
    out PI_STATUS_PORT, ax
    jmp .stop

; Input AL is 0..9. Suppress the spigot's initial provisional zero.
; Emits 3 followed by dot, then the first 100 decimal digits.
emit_digit:
    push ax
    cmp byte [skip_leading_zero], 0
    je .ready
    mov byte [skip_leading_zero], 0
    jmp .return

.ready:
    cmp word [digits_emitted], OUTPUT_DIGITS
    jae .return
    xor ah, ah
    add al, '0'
    out PI_CHAR_PORT, ax
    inc word [digits_emitted]
    cmp word [digits_emitted], 1
    jne .return
    mov ax, '.'
    out PI_CHAR_PORT, ax
.return:
    pop ax
    ret

align 2, db 0
carry_q           dw 0
candidate         dw 0
predigit          dw 0
held_nines        dw 0
digits_emitted    dw 0
outer_remaining   dw 0
skip_leading_zero db 1
align 2, db 0
array:
    times ARRAY_LEN dw 2
