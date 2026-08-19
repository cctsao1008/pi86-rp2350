; Native BIOS platform constants shared by all early V30 firmware.
;
; Keep these names project-specific. They describe the pi86-rp2350 companion
; contract, not a claim of IBM PC compatibility.

%ifndef PI86_BIOS_PLATFORM_INC
%define PI86_BIOS_PLATFORM_INC

%define PI86_BIOS_PHYSICAL_BASE 0xF0000
%define PI86_BIOS_SEGMENT       0xF000
%define PI86_DIAGNOSTIC_PORT    0x00E9

%endif
