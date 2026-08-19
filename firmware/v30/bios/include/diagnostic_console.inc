; Earliest Native BIOS console: byte writes to companion port 00E9h.
;
; These macros are deliberately stack-free and RAM-free. DX must be initialized
; once with PI86_DIAG_INIT before PI86_DIAG_PUTC is used. The present physical
; contract is output-only and has no flow control.

%ifndef PI86_BIOS_DIAGNOSTIC_CONSOLE_INC
%define PI86_BIOS_DIAGNOSTIC_CONSOLE_INC

%macro PI86_DIAG_INIT 0
    mov dx, PI86_DIAGNOSTIC_PORT
%endmacro

%macro PI86_DIAG_PUTC 1
    mov al, %1
    out dx, al
%endmacro

%macro PI86_DIAG_CRLF 0
    PI86_DIAG_PUTC 13
    PI86_DIAG_PUTC 10
%endmacro

%endif
