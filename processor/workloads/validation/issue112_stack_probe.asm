bits 16
cpu 8086
org 0

; Entry and stack come from the unmodified C9D reset stub.
; Use CS overrides so the first writes require neither DS nor stack setup here.
    cli
    mov ax, ss
    mov [cs:0800h], ax
    mov [cs:0802h], sp

    mov ax, 0a55ah
    push ax
    mov [cs:0804h], sp
    pop bx
    mov [cs:0806h], bx
    mov [cs:0808h], sp

; Keep the return IP even and known for the physical write trace.
    times 031h - ($ - $$) db 090h
    call subroutine
after_call:                          ; IP = 0034h
    mov [cs:080ch], sp
    mov word [cs:080eh], 0c9e1h
    align 2, db 090h
idle:
    jmp short idle

    align 2, db 090h
subroutine:
    mov [cs:080ah], sp
    ret

    align 2, db 090h
