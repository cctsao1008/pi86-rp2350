; Native BIOS platform constants shared by all early V30 firmware.
;
; Keep these names project-specific. They describe the pi86-rp2350 companion
; contract, not a claim of IBM PC compatibility.

%ifndef PI86_BIOS_PLATFORM_INC
%define PI86_BIOS_PLATFORM_INC

%define PI86_BIOS_PHYSICAL_BASE 0xF0000
%define PI86_BIOS_SEGMENT       0xF000
%define PI86_DIAGNOSTIC_PORT    0x00E9

; AI Bridge mailbox ports are deliberately separate from the early diagnostic
; console.  The first physical gate uses TX_DATA and CONTROL; STATUS and
; RX_DATA become live when the runtime-staged mailbox is introduced.
%define PI86_AI_STATUS_PORT     0x00E0
%define PI86_AI_TX_DATA_PORT    0x00E2
%define PI86_AI_RX_DATA_PORT    0x00E4
%define PI86_AI_CONTROL_PORT    0x00E6

%endif
