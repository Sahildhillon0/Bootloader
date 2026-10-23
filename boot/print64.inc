; =============================================================================
; print64.inc — long-mode console output (VGA text memory + COM1)
; =============================================================================
; Same output as print32.inc, rewritten for 64-bit mode where PUSHAD does not
; exist. Shares [vga_row] / [vga_col] with the 32-bit routine.
; =============================================================================

[BITS 64]

; -----------------------------------------------------------------------------
; putc64 — print AL
; -----------------------------------------------------------------------------
putc64:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    mov  ah, al

    mov  dx, COM1 + 5
.wait:
    in   al, dx
    test al, 0x20
    jz   .wait
    mov  al, ah
    mov  dx, COM1
    out  dx, al

    cmp  ah, 13
    je   .done
    cmp  ah, 10
    je   .newline
    movzx eax, byte [vga_row]
    imul eax, VGA_COLS
    movzx ebx, byte [vga_col]
    add  eax, ebx
    mov  byte [VGA_BASE + rax * 2], ah
    mov  byte [VGA_BASE + rax * 2 + 1], VGA_ATTR
    inc  byte [vga_col]
    cmp  byte [vga_col], VGA_COLS
    jb   .done
.newline:
    mov  byte [vga_col], 0
    inc  byte [vga_row]
    cmp  byte [vga_row], VGA_ROWS
    jb   .done
    mov  rsi, VGA_BASE + VGA_COLS * 2
    mov  rdi, VGA_BASE
    mov  rcx, VGA_COLS * (VGA_ROWS - 1) / 2
    rep  movsd
    mov  eax, (VGA_ATTR << 8 | ' ') * 0x00010001
    mov  rcx, VGA_COLS / 2
    rep  stosd
    mov  byte [vga_row], VGA_ROWS - 1
.done:
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

; -----------------------------------------------------------------------------
; print64 — print the NUL-terminated string at RSI
; -----------------------------------------------------------------------------
print64:
    push rax
    push rsi
.loop:
    lodsb
    test al, al
    jz   .done
    call putc64
    jmp  .loop
.done:
    pop  rsi
    pop  rax
    ret

[BITS 16]
