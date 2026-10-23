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
;   4. Find a file in the FAT12 root directory and load it.
;   5. Load the GDT and switch to 32-bit protected mode.
; =============================================================================

[BITS 16]

LOAD_SEG equ 0x1000             ; files are loaded to physical 0x10000

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

    ; --- FAT12: load the file -------------------------------------------------
    call detect_ext
    call fat12_init
    mov  si, file_name
    mov  dx, LOAD_SEG
    call fat12_load_file
    jc   .file_missing
    mov  [file_size], eax
    mov  si, msg_loaded
    call print16
    mov  ax, [file_size]
    call print_hex16
    mov  si, msg_loaded_tail
    call print16

    ; Echo the file to the console
    push ds
    mov  cx, [file_size]
    mov  ax, LOAD_SEG
    mov  ds, ax
    xor  si, si
.echo:
    lodsb
    call putc16
    loop .echo
    pop  ds

    ; --- Protected mode -------------------------------------------------------
    mov  si, msg_pm
    call print16

    ; Remember where the BIOS cursor is; VGA output continues from there
    mov  ah, 0x03
    xor  bh, bh
    int  0x10                   ; DH = row, DL = column
    mov  [vga_row], dh
    mov  byte [vga_col], 0

    cli                         ; no IVT in protected mode
    lgdt [gdt_descriptor]
    mov  eax, cr0
    or   eax, 1                 ; CR0.PE
    mov  cr0, eax
    jmp  dword CODE32_SEL:pm_entry  ; far jump loads CS, flushes the prefetch queue

.a20_fail:
    mov  si, msg_a20_fail
    jmp  fatal16
.e820_fail:
    mov  si, msg_e820_fail
    jmp  fatal16
.file_missing:
    mov  si, msg_file_missing
    jmp  fatal16

; -----------------------------------------------------------------------------
; disk_error — reached from read_sector after all retries fail
; -----------------------------------------------------------------------------
disk_error:
    mov  si, msg_disk_error
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

; =============================================================================
; 32-bit protected mode
; =============================================================================
[BITS 32]
pm_entry:
    mov  ax, DATA_SEL
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x7C00

    mov  esi, msg_pm_ok
    call print32

    mov  esi, msg_done
    call print32
.halt:
    cli
    hlt
    jmp  .halt
[BITS 16]

%include "print16.inc"
%include "print32.inc"
%include "gdt.inc"
%include "a20.inc"
%include "e820.inc"
%include "fat12.inc"
%include "disk.inc"

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
section .data

msg_hello:      db "[STAGE2] Running at 0x7E00, boot drive 0x", 0
msg_crlf:       db 13, 10, 0
msg_a20:        db "[STAGE2] A20 enabled", 13, 10, 0
msg_e820:       db "[STAGE2] E820 memory map: 0x", 0
msg_e820_tail:  db " entries", 13, 10, 0
file_name:      db "HELLO   TXT"
msg_loaded:     db "[STAGE2] Loaded HELLO.TXT from FAT12: 0x", 0
msg_loaded_tail: db " bytes", 13, 10, 0
msg_file_missing: db "[STAGE2] FATAL: file not found in FAT12 root", 13, 10, 0
msg_disk_error: db "[STAGE2] FATAL: disk read failed", 13, 10, 0
msg_pm:         db "[STAGE2] Entering protected mode", 13, 10, 0
msg_pm_ok:      db "[STAGE4] 32-bit protected mode, flat 4 GiB segments", 13, 10, 0
msg_done:       db "[STAGE2] Done, halting", 13, 10, 0
msg_a20_fail:   db "[STAGE2] FATAL: could not enable A20", 13, 10, 0
msg_e820_fail:  db "[STAGE2] FATAL: E820 memory map unavailable", 13, 10, 0

section .bss

global boot_drive
boot_drive:     resb 1
e820_count:     resw 1
use_ext:        resb 1
vga_row:        resb 1
vga_col:        resb 1
fat_root_lba:   resw 1
fat_root_secs:  resw 1
fat_data_lba:   resw 1
fat_file_size:  resd 1
file_size:      resd 1
