; =============================================================================
; stage1.asm — First-stage bootloader (MBR / Boot Sector)
; =============================================================================
; This is the very first code that runs when the machine powers on (BIOS path).
;
; BIOS loads exactly 512 bytes from LBA 0 (the MBR) into physical address
; 0x7C00, then jumps to it. We are running in REAL MODE: 16-bit, no memory
; protection, no paging, full BIOS interrupt access.
;
; Responsibilities of Stage 1:
;   1. Fix up segment registers (BIOS state is undefined on entry).
;   2. Set up a usable stack.
;   3. Print a banner message via BIOS INT 10h.
;   4. Load Stage 2 from disk into memory at 0x0500 using INT 13h.
;   5. Jump to Stage 2.
;
; Memory layout at boot (real mode, lower 1 MiB):
;
;   0x00000 – 0x003FF   IVT  (Interrupt Vector Table, 1 KiB)
;   0x00400 – 0x004FF   BDA  (BIOS Data Area, 256 bytes)
;   0x00500 – 0x07BFF   FREE (29.7 KiB) ← we load stage2 here at 0x0500
;   0x07C00 – 0x07DFF   MBR  (this code, 512 bytes, loaded by BIOS)
;   0x07E00 – 0x7FFFF   FREE (480 KiB)
;   0x80000 – 0x9FFFF   EBDA (Extended BIOS Data Area, ~128 KiB)
;   0xA0000 – 0xBFFFF   VGA  (Video memory, 128 KiB)
;   0xC0000 – 0xFFFFF   ROM  (BIOS, option ROMs, 256 KiB)
; =============================================================================

[BITS 16]               ; Assemble as 16-bit code (real mode)
[ORG 0x7C00]            ; Tell NASM our code is linked at 0x7C00

; -----------------------------------------------------------------------------
; Equates / Constants
; -----------------------------------------------------------------------------
STAGE2_LOAD_SEG     equ 0x0050  ; Segment where stage2 will be loaded
                                 ; 0x0050:0x0000 = physical 0x00500
STAGE2_LOAD_OFF     equ 0x0000
STAGE2_START_LBA    equ 1       ; Stage 2 starts at LBA sector 1 (0-indexed)
STAGE2_SECTOR_COUNT equ 4       ; How many 512-byte sectors to read (2 KiB)

; -----------------------------------------------------------------------------
; Entry Point
; -----------------------------------------------------------------------------
_start:
    ; ------------------------------------------------------------------
    ; STEP 1: Far jump to normalise CS.
    ;
    ; Some BIOSes jump to 0x07C0:0x0000, others to 0x0000:0x7C00.
    ; We enforce CS=0x0000 so all our segment arithmetic is consistent.
    ; ------------------------------------------------------------------
    jmp  0x0000:_init       ; Far jump → CS=0x0000, IP=_init

_init:
    ; ------------------------------------------------------------------
    ; STEP 2: Disable interrupts while setting up segments and stack.
    ; ------------------------------------------------------------------
    cli

    ; ------------------------------------------------------------------
    ; STEP 3: Zero out data segment registers.
    ; We use flat real mode: all segments = 0, offsets are raw addresses.
    ; ------------------------------------------------------------------
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; ------------------------------------------------------------------
    ; STEP 4: Set up the stack.
    ; Stack grows DOWN from 0x7C00. Since our MBR sits at 0x7C00 the
    ; stack will expand into 0x07BFF downward — safe, plenty of room.
    ; ------------------------------------------------------------------
    mov  sp, 0x7C00

    ; ------------------------------------------------------------------
    ; STEP 5: Re-enable interrupts. Stack is valid now.
    ; ------------------------------------------------------------------
    sti

    ; ------------------------------------------------------------------
    ; STEP 6: Save the boot drive number.
    ; BIOS stores the drive we booted from in DL (0x80 = first HDD).
    ; We save it to a variable so Stage 2 can use it.
    ; ------------------------------------------------------------------
    mov  [boot_drive], dl

    ; ------------------------------------------------------------------
    ; STEP 7: Print the boot banner.
    ; ------------------------------------------------------------------
    mov  si, msg_banner
    call print_string

    ; ------------------------------------------------------------------
    ; STEP 8: Load Stage 2 from disk.
    ; We call our disk read routine which wraps INT 13h / AH=0x02.
    ; ------------------------------------------------------------------
    mov  si, msg_loading
    call print_string

    call load_stage2

    ; ------------------------------------------------------------------
    ; STEP 9: Jump to Stage 2.
    ; Stage 2 was loaded at STAGE2_LOAD_SEG:STAGE2_LOAD_OFF.
    ; We do a far jump so CS is set correctly for Stage 2 to use.
    ; ------------------------------------------------------------------
    mov  si, msg_jumping
    call print_string

    jmp  STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

    ; Should never reach here — hang if something goes wrong
    jmp  halt


; =============================================================================
; load_stage2
; -----------------------------------------------------------------------------
; Uses BIOS INT 13h / AH=02h to read STAGE2_SECTOR_COUNT sectors starting
; at LBA STAGE2_START_LBA into memory at STAGE2_LOAD_SEG:STAGE2_LOAD_OFF.
;
; INT 13h / AH=02h register contract:
;   AH = 0x02  (read sectors function)
;   AL = sector count to read
;   CH = cylinder number (bits 7-0)
;   CL = sector number (bits 5-0) | cylinder bits 9-8 (bits 7-6)
;   DH = head number
;   DL = drive number (0x80 for first HDD)
;   ES:BX = destination buffer
;
; LBA → CHS conversion (for a standard 1.44MB floppy geometry):
;   Sectors Per Track  (SPT) = 18
;   Heads Per Cylinder (HPC) = 2
;
;   sector   = (LBA mod SPT) + 1
;   head     = (LBA / SPT)   mod HPC
;   cylinder = (LBA / SPT)   / HPC
; =============================================================================
SECTORS_PER_TRACK   equ 18
HEADS_PER_CYLINDER  equ 2

load_stage2:
    ; Set up the target buffer  ES:BX = STAGE2_LOAD_SEG:0x0000
    mov  ax, STAGE2_LOAD_SEG
    mov  es, ax
    mov  bx, STAGE2_LOAD_OFF

    ; Convert LBA STAGE2_START_LBA to CHS
    ; -- sector = (LBA mod SPT) + 1
    mov  ax, STAGE2_START_LBA
    xor  dx, dx
    mov  cx, SECTORS_PER_TRACK
    div  cx                     ; AX = LBA/SPT,  DX = LBA mod SPT
    mov  cl, dl
    inc  cl                     ; CL = sector number (1-based)

    ; -- head = (LBA/SPT) mod HPC
    xor  dx, dx
    mov  cx, HEADS_PER_CYLINDER
    div  cx                     ; AX = cylinder,  DX = head
    mov  dh, dl                 ; DH = head

    ; -- cylinder: AX = cylinder number
    mov  ch, al                 ; CH = cylinder (low 8 bits)
    ; (We ignore bits 9-8 of cylinder — fine for < 256 cylinders)

    ; Restore DL = boot drive
    mov  dl, [boot_drive]

    ; Issue the BIOS call with retry logic
    mov  ah, 0x02               ; Read sectors function
    mov  al, STAGE2_SECTOR_COUNT

    ; Retry up to 3 times (BIOS can fail on first try)
    mov  di, 3

.retry:
    pusha                       ; Save all registers (INT 13h may clobber them)
    int  0x13                   ; BIOS disk read
    popa                        ; Restore registers (re-load from our variables)

    ; Re-populate registers after popa (popa clobbers ax and flags)
    mov  ah, 0x02
    mov  al, STAGE2_SECTOR_COUNT
    mov  dl, [boot_drive]

    jnc  .success               ; CF=0 means success

    dec  di
    jnz  .retry

    ; All retries failed — print error and hang
    mov  si, msg_disk_error
    call print_string
    jmp  halt

.success:
    ret


; =============================================================================
; print_string
; -----------------------------------------------------------------------------
; Prints a null-terminated string pointed to by SI using BIOS INT 10h / AH=0Eh
; (TTY output, single character at a time).
;
; Clobbers: AX, BX, SI
; =============================================================================
print_string:
    mov  ah, 0x0E           ; BIOS teletype output function
    mov  bh, 0x00           ; Page number 0
    mov  bl, 0x0F           ; Text attribute: bright white on black

.loop:
    lodsb                   ; Load byte at [SI] into AL, advance SI
    test al, al             ; Check for null terminator
    jz   .done
    int  0x10               ; Print character in AL
    jmp  .loop

.done:
    ret


; =============================================================================
; halt — infinite loop on unrecoverable error
; =============================================================================
halt:
    cli
    hlt
    jmp  halt


; =============================================================================
; Data Section
; =============================================================================
boot_drive:     db 0            ; Saved boot drive number (set from DL at entry)

msg_banner:     db 0x0D, 0x0A
                db "============================", 0x0D, 0x0A
                db "  x86_64 Bootloader v0.1    ", 0x0D, 0x0A
                db "============================", 0x0D, 0x0A, 0

msg_loading:    db "  [STAGE1] Loading stage2...", 0x0D, 0x0A, 0
msg_jumping:    db "  [STAGE1] Jumping to stage2", 0x0D, 0x0A, 0
msg_disk_error: db 0x0D, 0x0A,  "  [ERROR]  Disk read failed!", 0x0D, 0x0A, 0


; =============================================================================
; Boot Sector Padding + Magic Number
; =============================================================================
; The MBR must be exactly 512 bytes. BIOS checks bytes 510-511 for the
; boot signature 0x55AA. If missing, BIOS will NOT execute this code.
; =============================================================================
times 510 - ($ - $$) db 0  ; Pad with zeros up to byte 510
dw 0xAA55                  ; Boot signature (little-endian: 0x55, 0xAA)
