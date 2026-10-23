; =============================================================================
; print16.inc — real-mode console output (BIOS screen + COM1)
; =============================================================================

COM1 equ 0x3F8

; -----------------------------------------------------------------------------
; serial_init — COM1 at 115200 baud, 8 data bits, no parity, 1 stop bit
; -----------------------------------------------------------------------------
serial_init:
    pusha
    mov  dx, COM1 + 1
    xor  al, al
    out  dx, al                 ; IER: no interrupts
    mov  dx, COM1 + 3
    mov  al, 0x80
    out  dx, al                 ; LCR: DLAB=1 to set the divisor
    mov  dx, COM1 + 0
    mov  al, 1
    out  dx, al                 ; divisor low  (115200 / 1)
    mov  dx, COM1 + 1
    xor  al, al
    out  dx, al                 ; divisor high
    mov  dx, COM1 + 3
    mov  al, 0x03
    out  dx, al                 ; LCR: 8N1, DLAB=0
    mov  dx, COM1 + 2
    mov  al, 0xC7
    out  dx, al                 ; FCR: enable + clear FIFOs, 14-byte threshold
    mov  dx, COM1 + 4
    mov  al, 0x03
    out  dx, al                 ; MCR: DTR + RTS
    popa
    ret

; -----------------------------------------------------------------------------
; putc16 — print the character in AL to COM1 and the screen
; -----------------------------------------------------------------------------
putc16:
    pusha
    mov  ah, al
    mov  dx, COM1 + 5           ; LSR
.wait:
    in   al, dx
    test al, 0x20               ; THR empty?
    jz   .wait
    mov  al, ah
    mov  dx, COM1
    out  dx, al
    mov  ah, 0x0E
    xor  bx, bx
    int  0x10
    popa
    ret

; -----------------------------------------------------------------------------
; print16 — print the NUL-terminated string at DS:SI
; -----------------------------------------------------------------------------
print16:
    pusha
.loop:
    lodsb
    test al, al
    jz   .done
    call putc16
    jmp  .loop
.done:
    popa
    ret

; -----------------------------------------------------------------------------
; print_hex16 — print AX as 4 hex digits
; -----------------------------------------------------------------------------
print_hex16:
    pusha
    mov  cx, 4
.digit:
    rol  ax, 4
    push ax
    and  al, 0x0F
    add  al, '0'
    cmp  al, '9'
    jbe  .out
    add  al, 'A' - '9' - 1
.out:
    call putc16
    pop  ax
    loop .digit
    popa
    ret
