bits 16

; Open Watcom's 16-bit code generator may lower near memcpy/memset operations
; to compiler helper calls even when the C runtime is deliberately not linked.
; The pinned 2026-09-01 C/16 build emits the following register contracts in
; the FreeRTOS queue/object code generated with -0 -ms -ecc -zu:
;
;   memcpy_: AX=dst, DX=src, BX=len
;   memset_: DI=dst, DX=value, BX=len
;
; Physical #60 validation exposed a queue payload mismatch because the previous
; helper implementation used a different register convention.  Keep the helper
; bodies conservative: preserve the caller-visible registers, operate entirely
; inside DGROUP (SS=DS=DGROUP), and use only Intel 8086 instructions.

segment _TEXT public align=16 class=CODE use16

global memcpy_
global memset_

memcpy_:
    ; AX=destination offset, DX=source offset, BX=byte count.
    ; Preserve AX so the original destination value is also available as the
    ; conventional memcpy return value if a generated caller observes it.
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov si, dx
    mov di, ax
    mov cx, bx
    push ds
    pop es
    cld
    rep movsb

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

memset_:
    ; DI=destination offset, DX=fill value (DL is the byte), BX=byte count.
    ; DI is live across generated calls in the pinned compiler, so preserve it
    ; together with the other caller-visible registers.
    pushf
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    mov cx, bx
    mov al, dl
    push ds
    pop es
    cld
    rep stosb

    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret
