; =============================================================================
; print32.inc — protected-mode console output (VGA text memory + COM1)
; =============================================================================
; No BIOS once we leave real mode: characters go straight into the 80x25 text
; buffer at 0xB8000 (2 bytes each: ASCII, attribute) and out of COM1.
; [vga_row] / [vga_col] start at the BIOS cursor so the real-mode log stays on
; screen; the kernel continues from the same position.
; =============================================================================

[BITS 32]

VGA_BASE  equ 0xB8000
VGA_COLS  equ 80
VGA_ROWS  equ 25
VGA_ATTR  equ 0x0A              ; light green on black

; -----------------------------------------------------------------------------
; putc32 — print AL
; -----------------------------------------------------------------------------
putc32:
    pushad
    mov  ah, al

    ; COM1
    mov  dx, COM1 + 5
.wait:
    in   al, dx
    test al, 0x20
    jz   .wait
    mov  al, ah
    mov  dx, COM1
    out  dx, al

    ; VGA
    cmp  ah, 13
    je   .done                  ; CR: the LF below does the work
    cmp  ah, 10
    je   .newline
    movzx eax, byte [vga_row]
    imul eax, VGA_COLS
    movzx ebx, byte [vga_col]
    add  eax, ebx
    mov  byte [VGA_BASE + eax * 2], ah
    mov  byte [VGA_BASE + eax * 2 + 1], VGA_ATTR
    inc  byte [vga_col]
    cmp  byte [vga_col], VGA_COLS
    jb   .done
.newline:
    mov  byte [vga_col], 0
    inc  byte [vga_row]
    cmp  byte [vga_row], VGA_ROWS
    jb   .done
    ; Scroll up one line and blank the last row
    mov  esi, VGA_BASE + VGA_COLS * 2
    mov  edi, VGA_BASE
    mov  ecx, VGA_COLS * (VGA_ROWS - 1) / 2
    rep  movsd
    mov  eax, (VGA_ATTR << 8 | ' ') * 0x00010001
    mov  ecx, VGA_COLS / 2
    rep  stosd
    mov  byte [vga_row], VGA_ROWS - 1
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; print32 — print the NUL-terminated string at ESI
; -----------------------------------------------------------------------------
print32:
    pushad
.loop:
    lodsb
    test al, al
    jz   .done
    call putc32
    jmp  .loop
.done:
    popad
    ret

[BITS 16]
