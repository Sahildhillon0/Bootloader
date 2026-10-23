; =============================================================================
; stage1.asm — First-stage bootloader (boot sector of a FAT12 floppy image)
; =============================================================================
; BIOS loads these 512 bytes from LBA 0 to 0x0000:0x7C00 and jumps here in
; 16-bit real mode, with the boot drive number in DL.
;
; The sector doubles as the FAT12 boot record: bytes 3..61 are the BIOS
; Parameter Block (BPB), so the image stays a valid FAT12 volume that mtools,
; Linux and the kernel can read. The BPB reserves 64 sectors at the front of
; the disk; sector 0 is this code and sectors 1..63 hold stage2.
;
; Responsibilities of Stage 1:
;   1. Normalise CS:IP, zero the segment registers, set up a stack.
;   2. Print a banner (BIOS INT 10h and COM1, so it shows up headless too).
;   3. Detect INT 13h extensions; fall back to CHS using the BPB geometry.
;   4. Load the reserved sectors 1..63 (stage2) to 0x0000:0x7E00.
;   5. Jump to stage2 with the boot drive still in DL.
;
; Disk layout (1.44 MB, 2880 sectors):
;   LBA  0        stage1 (this sector, with BPB)
;   LBA  1..63    stage2 (reserved sectors)
;   LBA 64..81    FAT #1 and FAT #2 (9 sectors each)
;   LBA 82..95    root directory (224 entries)
;   LBA 96..      data clusters (KERNEL.ELF, HELLO.TXT, ...)
;
; Real-mode memory layout used by the loader:
;   0x00000 – 0x004FF   IVT + BIOS Data Area
;   0x00500 – 0x07BFF   free; stack grows down from 0x7C00
;   0x07C00 – 0x07DFF   stage1 (this code)
;   0x07E00 – 0x0FFFF   stage2 (up to 63 sectors = 31.5 KiB)
; =============================================================================

[BITS 16]
[ORG 0x7C00]

STAGE2_ADDR  equ 0x7E00         ; physical = 0x0000:0x7E00

; -----------------------------------------------------------------------------
; Jump over the BPB. FAT requires EB xx 90 at offset 0.
; -----------------------------------------------------------------------------
    jmp  short _start
    nop

; -----------------------------------------------------------------------------
; BIOS Parameter Block — 1.44 MB floppy, 64 reserved sectors
; -----------------------------------------------------------------------------
bpb_oem:            db "BOOTLDR "   ; OEM name (8 bytes)
bpb_bytes_per_sec:  dw 512
bpb_sec_per_clus:   db 1
bpb_reserved:       dw 64           ; boot sector + stage2
bpb_num_fats:       db 2
bpb_root_entries:   dw 224
bpb_total_sectors:  dw 2880
bpb_media:          db 0xF0         ; 3.5" 1.44 MB
bpb_sec_per_fat:    dw 9
bpb_spt:            dw 18           ; sectors per track
bpb_heads:          dw 2
bpb_hidden:         dd 0
bpb_total_sec32:    dd 0
; Extended boot record
ebr_drive:          db 0
ebr_reserved:       db 0
ebr_signature:      db 0x29
ebr_volume_id:      dd 0x1BADB002
ebr_label:          db "BOOTLOADER "  ; 11 bytes
ebr_fs_type:        db "FAT12   "     ; 8 bytes

; -----------------------------------------------------------------------------
; Entry point
; -----------------------------------------------------------------------------
_start:
    ; Some BIOSes enter at 0x07C0:0x0000, others at 0x0000:0x7C00.
    ; A far jump pins CS=0 so every label below is a plain offset.
    jmp  0x0000:_init

_init:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00             ; stack grows down, away from our code
    sti
    cld

    mov  [boot_drive], dl

    mov  si, msg_banner
    call print_string

    call detect_ext

    ; --- Load stage2: reserved sectors 1 .. bpb_reserved-1 -------------------
    mov  si, msg_loading
    call print_string

    mov  ax, 1                  ; first LBA after the boot sector
    mov  bx, STAGE2_ADDR        ; ES = 0
.load:
    call read_sector
    add  bx, 512
    inc  ax
    cmp  ax, [bpb_reserved]
    jb   .load

    mov  si, msg_jumping
    call print_string

    mov  dl, [boot_drive]
    jmp  0x0000:STAGE2_ADDR

; -----------------------------------------------------------------------------
; disk_error — reached from read_sector after all retries fail
; -----------------------------------------------------------------------------
disk_error:
    mov  si, msg_disk_error
    call print_string
halt:
    cli
    hlt
    jmp  halt

; -----------------------------------------------------------------------------
; print_string — print the NUL-terminated string at DS:SI
; Writes each byte to COM1 and to the screen via INT 10h teletype.
; Clobbers: SI
; -----------------------------------------------------------------------------
print_string:
    pusha
.loop:
    lodsb
    test al, al
    jz   .done
    mov  dx, 0x3F8              ; COM1 data port
    out  dx, al
    mov  ah, 0x0E               ; teletype output
    xor  bx, bx                 ; page 0
    int  0x10
    jmp  .loop
.done:
    popa
    ret

%include "disk.inc"

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
boot_drive:     db 0
use_ext:        db 0

msg_banner:     db 13, 10, "x86_64 Bootloader v1.0", 13, 10, 0
msg_loading:    db "[STAGE1] Loading stage2...", 13, 10, 0
msg_jumping:    db "[STAGE1] Jumping to stage2", 13, 10, 0
msg_disk_error: db "[STAGE1] Disk read failed!", 13, 10, 0

; -----------------------------------------------------------------------------
; Pad to 510 bytes and append the boot signature
; -----------------------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55
