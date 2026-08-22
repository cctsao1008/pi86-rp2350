bits 16
org 0

%include "include/platform.inc"

; AI-B1-A bounded runtime mailbox consumer. The seven greeting words come
; from physical word reads at 00E4h, not from this ROM image. XOR verification
; proves what the V30 consumed before it publishes the canonical reply.
;
; Keep execution stack-free and forward-fetching until the final aligned
; checkpoint. The paired ROM sequencer can then retain the accepted C0C0
; known-path contract while the independent mailbox sequencer ignores fetches.

%define HOST_GREETING_WORDS 7
%define HOST_GREETING_XOR   0x7F20
%define PI86_AI_CONTROL_COMMIT 0x0001

global ai_bridge_runtime_entry
ai_bridge_runtime_entry:
    cli
    cld

    mov bx, HOST_GREETING_XOR
    mov dx, PI86_AI_RX_DATA_PORT

%rep HOST_GREETING_WORDS
    in ax, dx
    xor bx, ax
%endrep

    ; The passive observer requires a zero checksum at 00E8h. Execution does
    ; not branch on the result, so ROM fetch order remains bounded even when a
    ; mailbox response is wrong; the physical result still fails explicitly.
    mov dx, 0x00E8
    mov ax, bx
    out dx, ax

    mov dx, PI86_AI_TX_DATA_PORT

    mov ax, 0x4548             ; "HE"
    out dx, ax
    mov ax, 0x4C4C             ; "LL"
    out dx, ax
    mov ax, 0x204F             ; "O "
    out dx, ax
    mov ax, 0x504F             ; "OP"
    out dx, ax
    mov ax, 0x4E45             ; "EN"
    out dx, ax
    mov ax, 0x4941             ; "AI"
    out dx, ax
    mov ax, 0x4320             ; " C"
    out dx, ax
    mov ax, 0x444F             ; "OD"
    out dx, ax
    mov ax, 0x5845             ; "EX"
    out dx, ax

    mov dx, PI86_AI_CONTROL_PORT
    mov ax, PI86_AI_CONTROL_COMMIT
    out dx, ax

align 2, db 0x90
ai_bridge_runtime_checkpoint:
    jmp short ai_bridge_runtime_checkpoint
