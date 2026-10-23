; =============================================================================
; stage2.asm — Second-stage bootloader
; =============================================================================
; Loaded by stage1 to 0x0000:0x7E00 from the reserved sectors of the disk.
; Entered in 16-bit real mode with DL = boot drive.
;
; Stage 2 responsibilities:
;   1. Initialise COM1 so every step is visible on the serial console.
;   2. Enable the A20 line.
;   3. Read the BIOS E820 memory map.
; =============================================================================

[BITS 16]

extern __bss_start, __bss_end

section .text16 progbits alloc exec

global stage2_start
stage2_start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti
    cld

    ; Zero .bss (ld does not store it in the flat binary)
    mov  di, __bss_start
    mov  cx, __bss_end
    sub  cx, di
    xor  al, al
    rep  stosb

    mov  [boot_drive], dl

    call serial_init
    mov  si, msg_hello
    call print16
    mov  al, [boot_drive]
    xor  ah, ah
    call print_hex16
    mov  si, msg_crlf
    call print16

    ; --- A20 ------------------------------------------------------------------
    call enable_a20
    jc   .a20_fail
    mov  si, msg_a20
    call print16

    ; --- Memory map -----------------------------------------------------------
    call read_e820
    jc   .e820_fail
    mov  si, msg_e820
    call print16
    mov  ax, [e820_count]
    call print_hex16
    mov  si, msg_e820_tail
    call print16

    mov  si, msg_done
    call print16
    jmp  halt16

.a20_fail:
    mov  si, msg_a20_fail
    jmp  fatal16
.e820_fail:
    mov  si, msg_e820_fail
    jmp  fatal16

; -----------------------------------------------------------------------------
; fatal16 — print the message at SI and halt
; -----------------------------------------------------------------------------
fatal16:
    call print16
halt16:
    cli
    hlt
    jmp  halt16

%include "print16.inc"
%include "a20.inc"
%include "e820.inc"

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
section .data

msg_hello:      db "[STAGE2] Running at 0x7E00, boot drive 0x", 0
msg_crlf:       db 13, 10, 0
msg_a20:        db "[STAGE2] A20 enabled", 13, 10, 0
msg_e820:       db "[STAGE2] E820 memory map: 0x", 0
msg_e820_tail:  db " entries", 13, 10, 0
msg_done:       db "[STAGE2] Done, halting", 13, 10, 0
msg_a20_fail:   db "[STAGE2] FATAL: could not enable A20", 13, 10, 0
msg_e820_fail:  db "[STAGE2] FATAL: E820 memory map unavailable", 13, 10, 0

section .bss

global boot_drive
boot_drive:     resb 1
e820_count:     resw 1
