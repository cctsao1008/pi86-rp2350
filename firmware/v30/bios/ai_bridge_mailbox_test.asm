bits 16
org 0

%include "include/platform.inc"

; AI-B0 is the first physical, pre-Codex mailbox gate.  The RP2350 build
; stages the canonical host greeting in its internal-SRAM response table.  The
; real V30 reads all seven aligned words, validates their XOR, and only then
; emits the canonical reply through the mailbox TX port.
;
; This deliberately does not claim an asynchronous runtime mailbox.  STATUS
; and RX_DATA become active in AI-B1; AI-B0 proves native message consumption,
; native reply generation, separate mailbox I/O, and the existing PIO/DMA bus
; ownership path without adding USB/HID timing to the experiment.

%define HOST_GREETING_WORDS 7
%define HOST_GREETING_XOR   0x7F20
%define V30_REPLY_WORDS     9
%define PI86_AI_CONTROL_COMMIT 0x01

global ai_bridge_entry
ai_bridge_entry:
    cli
    cld
    ; RESET provides no usable stack.  Load DS directly from CS so the first
    ; mailbox experiment remains stack-free just like the Native BIOS baseline.
    mov ax, cs
    mov ds, ax

    mov si, host_greeting - $$
    mov cx, HOST_GREETING_WORDS
    mov bx, HOST_GREETING_XOR
.consume_host_message:
    lodsw
    xor bx, ax
    loop .consume_host_message

    or bx, bx
    jne ai_bridge_checkpoint

    mov si, v30_reply - $$
    ; LOOP left CX at zero.  OUTSW is native to the V30 and publishes two
    ; mailbox bytes per physical word-I/O cycle without touching the stack.
    mov cl, V30_REPLY_WORDS
    mov dx, PI86_AI_TX_DATA_PORT
    rep outsw

    mov dx, PI86_AI_CONTROL_PORT
    mov al, PI86_AI_CONTROL_COMMIT
    out dx, al
    jmp short ai_bridge_checkpoint

align 2, db 0x90
host_greeting:
    db "HELLO NEC V30", 0

v30_reply:
    db "HELLO OPENAI CODEX"

align 2, db 0x90
ai_bridge_checkpoint:
    jmp short ai_bridge_checkpoint
