bits 16
org 0

%include "include/platform.inc"

; Persistent RP86 processor runtime.
;
; The reset path invokes INT 60h before enabling maskable interrupts.  The
; software handler publishes a native heartbeat/notification record and
; returns with IRET.  The foreground then remains alive in STI/HLT; every
; physical companion interrupt wakes it, executes the host mailbox ISR, and
; returns to the same idle loop.  The instruction stream is deliberately
; compact and section-aligned so independent PIO response streams can retain
; their own exact keys while unrelated interrupt traffic is in flight.

%define COMPANION_VECTOR       0x20
%define NATIVE_SERVICE_VECTOR  0x60
%define COMPANION_ISR_OFFSET   0x0140
%define INT60_ISR_OFFSET       0x0100

%define STATUS_PORT            0x00E0
%define TX_PORT                0x00E2
%define RX_PORT                0x00E4
%define CONTROL_PORT           0x00E6
%define WITNESS_PORT           0x00E8
%define COMMIT                 0x0001
%define HOST_WORDS             7
%define CALCULATOR_MAGIC       0xCA1C
%define CALCULATOR_SLOT        0x0170

global rp86_processor_runtime_entry
rp86_processor_runtime_entry:
    cli
    cld
    xor ax, ax
    mov ss, ax
    mov sp, 0x8000

    ; Persistent native completion counters. General shared RAM is not yet a
    ; canonical runtime capability, so the physical processor owns these
    ; values in registers across STI/HLT and interrupt service:
    ;
    ;   ES:BP = completed physical IRQ services (32 bit)
    ;
    ; The ISR publishes all four words through its committed I/O reply. The
    ; RP2350 never invents or increments these processor-owned values.
    xor bp, bp
    mov es, ax

    ; Identify the installed physical processor without adding a branch or
    ; another instruction-fetch target. Intel 8086 evaluates undocumented
    ; AAD imm8 with the supplied base, while NEC V20/V30 retains base 10:
    ;
    ;   AX=0102h, AAD 10h -> AL=12h on Intel 8086
    ;   AX=0102h, AAD 10h -> AL=0Ch on NEC V20/V30
    ;
    ; DI owns this native signature for the lifetime of the STI/HLT runtime.
    ; It is published with every committed IRQ reply, so the Host declaration
    ; can be checked against evidence produced by the physical processor.
    mov ax, 0x0102
    db 0xD5, 0x10               ; undocumented AAD 16 discriminator
    xor ah, ah
    mov di, ax

    sti
    nop                         ; required interrupt-enable settling point
align 2, db 0x90
    ; Keep the first software interrupt immediately before the aligned
    ; persistent wait block. Counter initialization no longer fits in the
    ; historical 000Eh slot, so the complete foreground block begins at
    ; 001Eh and returns naturally at 0020h.
    times 0x001E - ($ - $$) db 0x90
RP86_EVEN_FETCH_TARGET companion_idle
    ; The initial V30 -> companion notification is a native software
    ; interrupt.  Its IRET target is naturally 0020h.
    int NATIVE_SERVICE_VECTOR

align 16, db 0x90
RP86_EVEN_FETCH_TARGET companion_wait
    ; NOP/HLT share the aligned word at 0020h.  A physical IRQ accepted from
    ; HLT stacks 0022h, so IRET never forces an unsupported odd-address ROM
    ; fetch.  On every wake the processor invokes INT 60h, returns at 0024h,
    ; and jumps back to the persistent wait point.
    nop
    hlt
RP86_EVEN_FETCH_TARGET companion_irq_resume
    int NATIVE_SERVICE_VECTOR
RP86_EVEN_FETCH_TARGET companion_cyclic_return
    jmp short companion_wait

    ; Keep boot/prefetch words out of the handler response windows.
    times INT60_ISR_OFFSET - ($ - $$) db 0x90

RP86_EVEN_FETCH_TARGET int60_handler
    ; A fixed early record is sufficient to prove the interrupt direction.
    ; Runtime sequence/status/retry fields live in the shared 64-byte ABI;
    ; later processor services may replace this payload without changing INT 60h.
    mov dx, TX_PORT
    mov ax, 0x4256              ; RP86 processor-service witness word
    out dx, ax
    mov dx, CONTROL_PORT
    mov ax, COMMIT
    out dx, ax
    iret

    ; Padding is executable NOP data only.  It keeps the physical-IRQ fetch
    ; path independent of any speculative words fetched around IRET.
    times COMPANION_ISR_OFFSET - ($ - $$) db 0x90

RP86_EVEN_FETCH_TARGET companion_irq_handler
    ; RP2350 asserts INTR only after an immutable seven-word mailbox record is
    ; ready.  There is therefore no current-cycle M33 lookup and no branch on
    ; STATUS in this validation image: every accepted physical IRQ consumes
    ; exactly one complete record.
    mov dx, STATUS_PORT
    in ax, dx

    ; The seven mailbox words also form the native calculator ABI:
    ;
    ;   CA1Ch, operation, lhs, rhs, reserved, reserved, reserved
    ;
    ; RP2350 selects one two-byte 8086 instruction in CALCULATOR_SLOT before
    ; asserting INTR.  The processor still owns both operands and the actual
    ; arithmetic.  All four instructions occupy exactly two bytes, so ADD,
    ; SUB, MUL, and DIV share one immutable fetch layout.
    xor bx, bx
    mov dx, RX_PORT
    in ax, dx
    xor bx, ax
    in ax, dx
    mov di, ax                  ; calculator operation
    xor bx, ax
    in ax, dx
    mov si, ax                  ; unsigned 16-bit lhs
    xor bx, ax
    in ax, dx
    mov cx, ax                  ; unsigned 16-bit rhs
    xor bx, ax
%rep HOST_WORDS - 4
    in ax, dx
    xor bx, ax
%endrep

    mov dx, WITNESS_PORT
    mov ax, bx
    out dx, ax

    ; Execute the selected arithmetic instruction on the physical processor.
    ; Default ROM bytes are ADD AX,CX (01 C8).  The RP2350 response table may
    ; substitute SUB AX,CX (29 C8), MUL CX (F7 E1), or DIV CX (F7 F1) at this
    ; exact aligned word without changing a single subsequent fetch address.
    mov ax, si
    xor dx, dx
    times CALCULATOR_SLOT - ($ - $$) db 0x90
RP86_EVEN_FETCH_TARGET calculator_dispatch
    ; The default response words retain the accepted inline ADD behavior.
    ; When a Host-staged calculator workload is RUNNING, the RP2350 replaces
    ; these six bytes with NOP + CALL FAR imm16:16.  The physical processor
    ; then fetches the selected four-byte entry from Internal SRAM and RETF
    ; returns at the same aligned continuation below.
    add ax, cx
    times 4 db 0x90

    ; Publish six processor-produced words.  The passive observer must see
    ; this exact record before the Host exposes a calculator result:
    ; result-low, result-high/remainder, magic, operation, lhs, rhs.
RP86_EVEN_FETCH_TARGET calculator_return
    out TX_PORT, ax
    mov ax, dx
    out TX_PORT, ax
    mov ax, CALCULATOR_MAGIC
    out TX_PORT, ax
    mov ax, di
    out TX_PORT, ax
    mov ax, si
    out TX_PORT, ax
    mov ax, cx
    out TX_PORT, ax

    ; Recreate the native processor signature after DI served as the operation
    ; register.  0012h is Intel 8086 behavior; 000Ch is NEC V20/V30 behavior.
    mov ax, 0x0102
    db 0xD5, 0x10
    xor ah, ah
    mov di, ax
    out TX_PORT, ax
    mov dx, TX_PORT

    ; Count a completed native service immediately before publishing the
    ; counter snapshot and commit. ADD/ADC removes the carry branch entirely,
    ; so a counter rollover cannot introduce another instruction-fetch target.
    add bp, 1
    mov ax, es
    adc ax, 0
    mov es, ax

    ; Publish three identical copies. The RP2350 accepts a counter only when
    ; at least two copies agree, so one mistimed passive data sample cannot
    ; become a false processor-owned sequence.
    mov ax, bp
    out dx, ax
    mov ax, es
    out dx, ax
    mov ax, bp
    out dx, ax
    mov ax, es
    out dx, ax
    mov ax, bp
    out dx, ax
    mov ax, es
    out dx, ax

    mov dx, CONTROL_PORT
    mov ax, COMMIT
    out dx, ax

    ; There is no external 8259 on the present HAT.  This write is retained as
    ; the native EOI contract; the RP2350 companion owns the corresponding
    ; in-service state and accepts the next physical interrupt only afterward.
    mov dx, 0x0020
    mov ax, 0x0020
    out dx, ax
    iret

align 2, db 0x90
rp86_processor_runtime_end:
    nop
