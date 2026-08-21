bits 16
org 0

; PC1-C0C1-B2-C two-word and byte-lane coherence proof.
; Every read result is mirrored to diagnostic I/O port 00E8h so the passive
; observer proves what the V30 consumed, not only what PIO drove. The compact
; 44-byte image remains well inside the bounded 32-entry Epoch-A ROM table.

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

    ; Epoch A deliberately leaves RAM reads unsupported, so never derive a
    ; later write payload from such a read. Reload the known byte before each
    ; write; Epoch B can then prove that the PIO-local slot returns it.
    mov al, 0x34
    mov [0x0104], al
    mov al, [0x0104]
    out dx, al

    ; Writing the same known byte to odd 0105h exercises the physical high
    ; lane. The later high-lane read must route that byte back into AL.
    mov al, 0x34
    mov [0x0105], al
    mov al, [0x0105]
    out dx, al

checkpoint:
    jmp checkpoint
