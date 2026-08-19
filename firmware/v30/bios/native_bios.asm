bits 16
org 0

%include "include/platform.inc"
%include "include/diagnostic_console.inc"

; Native BIOS foundation payload, currently mapped at physical F0000h by the
; descriptor-fed PC1-C0C0 regression harness. The harness separately serves
; the architectural FFFF0 far-jump vector until the general C0C1 ROM mapper can
; expose a complete BIOS address window.
;
; This first payload is intentionally stack-free, RAM-free, and strictly
; forward-fetching. Those constraints belong to the golden-HAT regression,
; not to the eventual BIOS API. They let the same real V30 path validate the
; source organization, platform constants, and diagnostic-console contract.

global bios_entry
bios_entry:
    cli
    cld
    PI86_DIAG_INIT

    PI86_DIAG_PUTC 'P'
    PI86_DIAG_PUTC 'I'
    PI86_DIAG_PUTC '8'
    PI86_DIAG_PUTC '6'
    PI86_DIAG_PUTC ' '
    PI86_DIAG_PUTC 'B'
    PI86_DIAG_PUTC 'I'
    PI86_DIAG_PUTC 'O'
    PI86_DIAG_PUTC 'S'
    PI86_DIAG_CRLF

bios_checkpoint:
    jmp short bios_checkpoint

align 2, db 0x90
