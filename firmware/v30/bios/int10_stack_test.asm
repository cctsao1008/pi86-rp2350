bits 16
org 0

; PC1-C0C1-B2-A bounded software-interrupt and stack transaction payload.
;
; The companion supplies an IVT entry for INT 10h at physical 00040h/00042h.
; SS:SP starts at 0000:0800, so the V30 writes FLAGS, CS, and return IP to
; 007FEh, 007FCh, and 007FAh. Epoch A passively learns those real write words.
; Epoch B adds exactly those three addresses to the current-address response
; table and requires IRET to return to bios_checkpoint.
;
; Keep int10_handler fixed at offset 0018h. The harness deliberately derives
; the IVT value from this documented ABI instead of predicting instruction
; fetch order or current-cycle stack data.

%define PI86_DIAGNOSTIC_PORT 0x00E9
%define INT10_HANDLER_OFFSET 0x0018

global bios_entry
bios_entry:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x0800
    mov al, 'I'
    int 0x10

bios_checkpoint:
    jmp short bios_checkpoint

times INT10_HANDLER_OFFSET - ($ - $$) db 0x90

int10_handler:
    mov dx, PI86_DIAGNOSTIC_PORT
    out dx, al
    iret

align 2, db 0x90
