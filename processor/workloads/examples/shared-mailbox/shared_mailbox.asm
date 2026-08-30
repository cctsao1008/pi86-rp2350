bits 16
org 0

; Polling demonstration for the canonical 3F000h shared mailbox. The Host
; commits a request by transferring owner to PROCESSOR. This physical CPU
; uppercases the payload in place, publishes its length/status, and transfers
; owner back to HOST as the final write.

MAILBOX_SEGMENT       equ 03F00h
OWNER_OFFSET         equ 8
STATUS_OFFSET        equ 10
REQUEST_LENGTH       equ 16
RESPONSE_LENGTH      equ 18
DATA_OFFSET          equ 32
OWNER_HOST           equ 1
OWNER_PROCESSOR      equ 2
STATUS_REQUEST_READY equ 1
STATUS_PROCESSING    equ 2
STATUS_RESULT_READY  equ 3
STATUS_ERROR         equ 4
DATA_CAPACITY        equ 4064
DIAGNOSTIC_PORT      equ 00E9h

start:
    cli
    cld
    mov ax, MAILBOX_SEGMENT
    mov ds, ax

poll:
    cmp word [OWNER_OFFSET], OWNER_PROCESSOR
    jne poll
    cmp word [STATUS_OFFSET], STATUS_REQUEST_READY
    jne bad_request
    mov word [STATUS_OFFSET], STATUS_PROCESSING
    mov cx, [REQUEST_LENGTH]
    cmp cx, DATA_CAPACITY
    ja bad_request
    mov si, DATA_OFFSET

convert:
    jcxz publish
    mov al, [si]
    cmp al, 'a'
    jb unchanged
    cmp al, 'z'
    ja unchanged
    sub al, 'a' - 'A'
    mov [si], al
unchanged:
    inc si
    loop convert

publish:
    mov ax, [REQUEST_LENGTH]
    mov [RESPONSE_LENGTH], ax
    mov word [STATUS_OFFSET], STATUS_RESULT_READY
    mov word [OWNER_OFFSET], OWNER_HOST
    jmp poll

bad_request:
    mov word [RESPONSE_LENGTH], 0
    mov word [STATUS_OFFSET], STATUS_ERROR
    mov word [OWNER_OFFSET], OWNER_HOST
    jmp poll
