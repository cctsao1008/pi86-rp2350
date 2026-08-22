bits 16
org 0

%include "include/platform.inc"

; AI-B1-B bounded live-publication consumer.
;
; The first STATUS read must return NOT_READY. During the forward-only delay,
; Core0 observes the completed STATUS match and starts the immutable key and
; response DMA streams. The second STATUS read must return READY. Keeping the
; path forward-only preserves the exact bounded ROM sequencer contract.

%define HOST_GREETING_WORDS 7
%define HOST_GREETING_XOR   0x7F20
%define PI86_AI_CONTROL_COMMIT 0x0001

global ai_bridge_live_entry
ai_bridge_live_entry:
    cli
    cld

    mov dx, PI86_AI_STATUS_PORT
    in ax, dx
    mov bx, ax

    ; Give Core0 a bounded publication window without servicing USB or looking
    ; up the current V30 cycle. These NOPs are ordinary sequential ROM fetches.
    times 128 nop

    in ax, dx
    mov cx, ax

    ; Publish a physical transition witness: expected (NOT_READY << 8)|READY
    ; is 0001h. The later write of zero on the same port is the payload XOR.
    mov ax, bx
    mov ah, al
    mov al, cl
    mov dx, 0x00E8
    out dx, ax

    mov bx, HOST_GREETING_XOR
    mov dx, PI86_AI_RX_DATA_PORT

%rep HOST_GREETING_WORDS
    in ax, dx
    xor bx, ax
%endrep

    mov dx, 0x00E8
    mov ax, bx
    out dx, ax

    mov dx, PI86_AI_TX_DATA_PORT

    mov ax, 0x4548
    out dx, ax
    mov ax, 0x4C4C
    out dx, ax
    mov ax, 0x204F
    out dx, ax
    mov ax, 0x504F
    out dx, ax
    mov ax, 0x4E45
    out dx, ax
    mov ax, 0x4941
    out dx, ax
    mov ax, 0x4320
    out dx, ax
    mov ax, 0x444F
    out dx, ax
    mov ax, 0x5845
    out dx, ax

    mov dx, PI86_AI_CONTROL_PORT
    mov ax, PI86_AI_CONTROL_COMMIT
    out dx, ax

align 2, db 0x90
ai_bridge_live_checkpoint:
    jmp short ai_bridge_live_checkpoint
