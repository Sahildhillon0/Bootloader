; =============================================================================
; disk.inc — BIOS disk access shared by stage1 and stage2 (16-bit real mode)
; =============================================================================
; The including file must define:
;   boot_drive  byte   BIOS drive number (DL at boot)
;   use_ext     byte   1 if INT 13h extensions (AH=42h) are available
;   bpb_spt     word   sectors per track  (CHS fallback geometry)
;   bpb_heads   word   number of heads    (CHS fallback geometry)
;   disk_error  label  jumped to when a read keeps failing
;
; Reads go one sector at a time. That keeps the CHS path from ever crossing a
; track boundary and keeps every transfer inside one 64 KiB DMA window.
; =============================================================================

; -----------------------------------------------------------------------------
; detect_ext — set use_ext if the BIOS supports INT 13h extensions for the drive
; Clobbers: nothing
; -----------------------------------------------------------------------------
detect_ext:
    pusha
    mov  byte [use_ext], 0
    mov  ah, 0x41
    mov  bx, 0x55AA
    mov  dl, [boot_drive]
    int  0x13
    jc   .done                  ; CF=1: no extensions
    cmp  bx, 0xAA55
    jne  .done
    test cl, 1                  ; bit 0: fixed disk access subset (AH=42h)
    jz   .done
    mov  byte [use_ext], 1
.done:
    popa
    ret

; -----------------------------------------------------------------------------
; read_sector — read one 512-byte sector
;   AX    = LBA (16-bit is plenty: a 1.44 MB floppy has 2880 sectors)
;   ES:BX = destination buffer
; Retries 5 times with a disk reset in between, then jumps to disk_error.
; Clobbers: nothing
; -----------------------------------------------------------------------------
read_sector:
    pusha
    mov  di, 5                  ; attempts left

.try:
    pusha                       ; INT 13h is free to clobber registers
    cmp  byte [use_ext], 0
    je   .chs

    ; --- LBA path: fill in the Disk Address Packet ---
    mov  [dap.lba], ax
    mov  [dap.offset], bx
    mov  [dap.segment], es
    mov  si, dap
    mov  ah, 0x42
    mov  dl, [boot_drive]
    int  0x13
    jmp  .called

.chs:
    ; --- CHS path ---
    ; sector   = (LBA mod SPT) + 1
    ; head     = (LBA / SPT) mod HEADS
    ; cylinder = (LBA / SPT) / HEADS
    xor  dx, dx
    div  word [bpb_spt]         ; AX = LBA / SPT, DX = LBA mod SPT
    mov  cl, dl
    inc  cl                     ; CL[5:0] = sector (1-based)
    xor  dx, dx
    div  word [bpb_heads]       ; AX = cylinder, DX = head
    mov  ch, al                 ; CH = cylinder bits 7..0
    shl  ah, 6
    or   cl, ah                 ; CL[7:6] = cylinder bits 9..8
    mov  dh, dl                 ; DH = head
    mov  dl, [boot_drive]
    mov  ax, 0x0201             ; AH=02 read, AL=1 sector
    int  0x13

.called:
    popa                        ; POPA leaves FLAGS alone, CF is still the result
    jnc  .ok

    dec  di
    jz   disk_error
    push ax
    xor  ah, ah                 ; AH=00: reset disk system
    mov  dl, [boot_drive]
    int  0x13
    pop  ax
    jmp  .try

.ok:
    popa
    ret

; Disk Address Packet for AH=42h
align 4
dap:
    db 0x10                     ; packet size
    db 0                        ; reserved
    dw 1                        ; sectors to transfer
.offset:  dw 0
.segment: dw 0
.lba:     dd 0                  ; LBA bits 31..0 (only the low word is written)
          dd 0                  ; LBA bits 63..32
