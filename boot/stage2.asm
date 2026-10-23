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
;   4. Load KERNEL.ELF from the FAT12 root directory to 0x10000.
;   5. Load the GDT and switch to 32-bit protected mode.
;   6. Build identity-mapped page tables and switch to 64-bit long mode.
;   7. Hand the ELF image to elf_load() (C), then jump to the kernel entry
;      point with RDI = &boot_info.
; =============================================================================

[BITS 16]
DEFAULT REL                     ; 64-bit code: RIP-relative where possible

LOAD_SEG    equ 0x1000          ; KERNEL.ELF is read to physical 0x10000
KERNEL_MAX  equ 0x50000         ; ...and must end before the FAT buffers at 0x60000
STACK64_TOP equ 0x80000         ; long-mode stack, grows down below the EBDA

extern __bss_start, __bss_end
extern elf_load                 ; boot/elf_loader.c

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

    ; --- FAT12: load KERNEL.ELF ----------------------------------------------
    call detect_ext
    call fat12_init
    mov  dword [fat_max_size], KERNEL_MAX
    mov  si, kernel_name
    mov  dx, LOAD_SEG
    call fat12_load_file
    jc   .kernel_error
    mov  [kernel_size], eax
    mov  si, msg_loaded
    call print16
    mov  ax, [kernel_size + 2]
    call print_hex16
    mov  ax, [kernel_size]
    call print_hex16
    mov  si, msg_loaded_tail
    call print16

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
.kernel_error:
    mov  si, msg_kernel_missing
    test eax, eax               ; EAX = 0: not found, else the oversized length
    jz   fatal16
    mov  si, msg_kernel_big
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

    call check_long_mode
    jc   .no_lm
    call setup_paging
    mov  esi, msg_lm
    call print32
    jmp  enter_long_mode

.no_lm:
    mov  esi, msg_no_lm
    call print32
.halt:
    cli
    hlt
    jmp  .halt

; =============================================================================
; 64-bit long mode
; =============================================================================
[BITS 64]
lm_entry:
    mov  ax, DATA_SEL
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  rsp, STACK64_TOP

    mov  rsi, msg_lm_ok
    call print64

    ; --- Load the kernel segments (C) ----------------------------------------
    mov  edi, LOAD_SEG << 4     ; image
    mov  esi, [kernel_size]     ; size
    lea  rdx, [boot_info]       ; boot_info, filled in on success
    call elf_load
    test rax, rax
    jnz  .elf_error

    mov  rsi, msg_elf_ok
    call print64

    ; --- Hand-off -------------------------------------------------------------
    mov  dword [boot_info.magic], BOOT_INFO_MAGIC
    mov  al, [boot_drive]
    mov  [boot_info.drive], al
    movzx eax, word [e820_count]
    mov  [boot_info.mmap_count], eax
    mov  qword [boot_info.mmap_addr], E820_MAP_ADDR
    mov  al, [vga_row]          ; last thing: the cursor after our final message
    mov  [boot_info.vga_row], al
    mov  al, [vga_col]
    mov  [boot_info.vga_col], al

    lea  rdi, [boot_info]
    jmp  [boot_info.entry]

.elf_error:
    push rax
    mov  rsi, msg_elf_fail
    call print64
    pop  rsi
    call print64
    mov  rsi, msg_crlf
    call print64
.halt:
    cli
    hlt
    jmp  .halt
[BITS 16]

%include "print16.inc"
%include "print32.inc"
%include "print64.inc"
%include "paging.inc"
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
kernel_name:    db "KERNEL  ELF"
msg_loaded:     db "[STAGE3] Loaded KERNEL.ELF from FAT12: 0x", 0
msg_loaded_tail: db " bytes", 13, 10, 0
msg_kernel_missing: db "[STAGE3] FATAL: KERNEL.ELF not found in FAT12 root", 13, 10, 0
msg_kernel_big: db "[STAGE3] FATAL: KERNEL.ELF larger than 320 KiB", 13, 10, 0
msg_disk_error: db "[STAGE2] FATAL: disk read failed", 13, 10, 0
msg_pm:         db "[STAGE2] Entering protected mode", 13, 10, 0
msg_pm_ok:      db "[STAGE4] 32-bit protected mode, flat 4 GiB segments", 13, 10, 0
msg_lm:         db "[STAGE4] Page tables built, entering long mode", 13, 10, 0
msg_no_lm:      db "[STAGE4] FATAL: CPU does not support 64-bit long mode", 13, 10, 0
msg_lm_ok:      db "[STAGE5] 64-bit long mode, first 1 GiB identity mapped", 13, 10, 0
msg_elf_ok:     db "[STAGE6] ELF segments loaded, jumping to kernel", 13, 10, 0
msg_elf_fail:   db "[STAGE6] FATAL: bad kernel ELF: ", 0
msg_a20_fail:   db "[STAGE2] FATAL: could not enable A20", 13, 10, 0
msg_e820_fail:  db "[STAGE2] FATAL: E820 memory map unavailable", 13, 10, 0

; -----------------------------------------------------------------------------
; boot_info — handed to the kernel; layout must match include/bootinfo.h
; -----------------------------------------------------------------------------
BOOT_INFO_MAGIC equ 0xB007B007

align 8
boot_info:
.magic:         dd 0
.drive:         db 0
.vga_row:       db 0
.vga_col:       db 0
                db 0
.mmap_count:    dd 0
                dd 0
.mmap_addr:     dq 0
.kernel_start:  dq 0            ; written by elf_load()
.kernel_end:    dq 0
.entry:         dq 0

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
fat_max_size:   resd 1
kernel_size:    resd 1
