bits 16
org 0

; Canonical Internal-SRAM native calculator workload.
;
; The persistent companion runtime enters one of four four-byte far-call
; targets with AX=lhs, CX=rhs and DX=0.  Each target performs the arithmetic
; on the physical 8086-class processor and returns with RETF.  The RP2350
; supplies bytes from the Host-staged Internal SRAM backing; it does not
; evaluate the expression.
;
; Entry offsets are part of the first executable workload ABI:
;   +0  unsigned add
;   +4  unsigned subtract
;   +8  unsigned multiply (DX:AX)
;   +12 unsigned divide   (AX quotient, DX remainder)

calculator_add:
    add ax, cx
    retf
    nop

calculator_sub:
    sub ax, cx
    retf
    nop

calculator_mul:
    mul cx
    retf
    nop

calculator_div:
    div cx
    retf
    nop
