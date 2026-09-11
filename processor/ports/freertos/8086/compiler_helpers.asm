bits 16

; Open Watcom's 16-bit code generator may lower near memcpy/memset operations
; to compiler helper calls even when the C runtime is deliberately not linked.
; These entry points implement the compiler-documented small-data helper ABI:
;
;   memcpy_: DI=dst, SI=src, CX=len -> DI=original dst; clobbers ES,SI,CX
;   memset_: DI=dst, AL=value, CX=len -> DI=original dst; clobbers ES,CX
;
; RP86 FreeRTOS v1 maintains SS=DS=DGROUP for every C execution context and
; uses near data pointers, so both source and destination offsets are in DS.
; Pure Intel 8086 instructions only.

segment _TEXT public align=16 class=CODE use16

global memcpy_
global memset_

memcpy_:
    push di
    push ds
    pop es
    cld
    rep movsb
    pop di
    ret

memset_:
    push di
    push ds
    pop es
    cld
    rep stosb
    pop di
    ret
