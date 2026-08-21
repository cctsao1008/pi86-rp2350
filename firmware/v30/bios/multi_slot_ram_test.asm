bits 16
org 0

; PC1-C0C1-B2-C two-word and byte-lane coherence proof.
; Every read result is mirrored to diagnostic I/O port 00E8h so the passive
; observer proves what the V30 consumed, not only what PIO drove. The compact
; 40-byte image keeps the bounded Epoch-A live ROM set at ordinal 23.

start:
    cli
    mov dx, 0x00E8

    mov ax, 0x1234
    mov [0x0100], ax
    mov ax, 0x5678
    mov [0x0102], ax

    ; Read in reverse order to prove the two PIO-local slots are independent.
    mov ax, [0x0102]
    out dx, ax
    mov ax, [0x0100]
    out dx, ax

    ; AL is 34h after the WORD0 read. Slot 3 is reused only after the complete
    ; LOW write/read/output sequence has finished.
    mov [0x0104], al
    mov al, [0x0104]
    out dx, al

    ; Writing AL to odd 0105h exercises the physical high byte lane. The V30
    ; routes the later high-lane read back into AL before the low-lane I/O proof.
    mov [0x0105], al
    mov al, [0x0105]
    out dx, al

checkpoint:
    jmp checkpoint
