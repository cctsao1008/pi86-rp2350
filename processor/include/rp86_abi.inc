; Canonical processor-visible I/O and interrupt ABI.
; Keep numerically identical to firmware/runtime/processor_abi.h.

%define RP86_INTERRUPT_VECTOR_COMPANION           0x20
%define RP86_INTERRUPT_VECTOR_NATIVE_SERVICE      0x60
%define RP86_IVT_COMPANION_OFFSET_ADDRESS         0x0080
%define RP86_IVT_COMPANION_SEGMENT_ADDRESS        0x0082
%define RP86_IVT_NATIVE_SERVICE_OFFSET_ADDRESS    0x0180
%define RP86_IVT_NATIVE_SERVICE_SEGMENT_ADDRESS   0x0182

%define RP86_IO_PORT_PIC_COMMAND                  0x0020
%define RP86_IO_PORT_STATUS                       0x00E0
%define RP86_IO_PORT_TX                           0x00E2
%define RP86_IO_PORT_RX                           0x00E4
%define RP86_IO_PORT_CONTROL                      0x00E6
%define RP86_IO_PORT_RESULT                       0x00E8
%define RP86_IO_PORT_DIAGNOSTIC                   0x00E9
%define RP86_IO_PORT_EXECUTION_CLOCK              0x00EA

%define RP86_CONTROL_IDLE_PREPARE                 0x0001
%define RP86_EXECUTION_CLOCK_REQUEST_FREE_RUNNING 0x0001
%define RP86_EXECUTION_CLOCK_REQUEST_CLOCK_STEPPED 0x0002

%define RP86_PROCESSOR_SIGNATURE_INTEL_8086        0x0012
%define RP86_PROCESSOR_SIGNATURE_NEC_V30           0x000C

%define RP86_PROCESSOR_ADDRESS_SPACE_SIZE          0x100000
%define RP86_INTERNAL_SRAM_PROCESSOR_BASE          0x00000
%define RP86_INTERNAL_SRAM_PROCESSOR_SIZE          0x40000
%define RP86_SHARED_MAILBOX_BASE                   0x3F000
%define RP86_SHARED_MAILBOX_SIZE                   0x1000
