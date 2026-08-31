#ifndef RP86_PROCESSOR_ABI_H
#define RP86_PROCESSOR_ABI_H

/* Canonical processor-visible I/O and interrupt ABI. */
#define RP86_INTERRUPT_VECTOR_COMPANION          0x20u
#define RP86_INTERRUPT_VECTOR_NATIVE_SERVICE     0x60u
#define RP86_IVT_COMPANION_OFFSET_ADDRESS        0x0080u
#define RP86_IVT_COMPANION_SEGMENT_ADDRESS       0x0082u
#define RP86_IVT_NATIVE_SERVICE_OFFSET_ADDRESS   0x0180u
#define RP86_IVT_NATIVE_SERVICE_SEGMENT_ADDRESS  0x0182u

#define RP86_IO_PORT_PIC_COMMAND                 0x0020u
#define RP86_IO_PORT_STATUS                      0x00E0u
#define RP86_IO_PORT_TX                          0x00E2u
#define RP86_IO_PORT_RX                          0x00E4u
#define RP86_IO_PORT_CONTROL                     0x00E6u
#define RP86_IO_PORT_RESULT                      0x00E8u
#define RP86_IO_PORT_DIAGNOSTIC                  0x00E9u
#define RP86_IO_PORT_EXECUTION_CLOCK             0x00EAu

#define RP86_CONTROL_IDLE_PREPARE                0x0001u
#define RP86_EXECUTION_CLOCK_REQUEST_FREE_RUNNING 0x0001u
#define RP86_EXECUTION_CLOCK_REQUEST_CLOCK_STEPPED 0x0002u

#endif
