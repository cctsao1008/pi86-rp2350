bits 16
cpu 8086
org 0200h

; Use the original C9O initializer as read-only data at 1000:0100.
; Record the two actual operands, one aligned code word, and one split word.
    cli
    mov ax,[cs:0102h]
    mov [cs:0870h],ax
    mov ax,[cs:0116h]
    mov [cs:0872h],ax
    mov ax,[cs:0104h]
    mov [cs:0874h],ax
    mov ax,[cs:015bh]
    mov [cs:0876h],ax

    mov ss,[cs:0102h]
    mov sp,[cs:0116h]
    mov [cs:0878h],ss
    mov [cs:087ah],sp
    jmp 1000h:0000h

    align 2, db 090h
