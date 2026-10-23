; =============================================================================
; a20.inc — enable the A20 address line (real mode)
; =============================================================================
; With A20 off, bit 20 of every address is forced to 0 and memory above 1 MiB
; wraps around, the way it did on the 8086. The kernel loads at 1 MiB, so A20
; must be on. Three methods, cheapest first, verified after each one.
; =============================================================================

; -----------------------------------------------------------------------------
; check_a20 — ZF=0 if A20 is enabled, ZF=1 if memory wraps at 1 MiB
; Compares 0x0000:0x0500 with 0xFFFF:0x0510 (= physical 0x100500).
; -----------------------------------------------------------------------------
check_a20:
    pusha
    push ds
    push es
    xor  ax, ax
    mov  ds, ax
    not  ax
    mov  es, ax                 ; ES = 0xFFFF
    mov  si, 0x0500
    mov  di, 0x0510
    mov  al, [ds:si]
    push ax                     ; save both bytes
    mov  al, [es:di]
    push ax
    mov  byte [ds:si], 0x00
    mov  byte [es:di], 0xFF
    cmp  byte [ds:si], 0xFF     ; ZF=1 → the write to 0x100500 hit 0x000500
    pop  ax
    mov  [es:di], al
    pop  ax
    mov  [ds:si], al
    pop  es
    pop  ds
    popa
    ret

; -----------------------------------------------------------------------------
; enable_a20 — CF=0 on success
; -----------------------------------------------------------------------------
enable_a20:
    call check_a20
    jnz  .ok

    ; 1) BIOS: INT 15h AX=2401h
    mov  ax, 0x2401
    int  0x15
    call check_a20
    jnz  .ok

    ; 2) Fast A20: bit 1 of system control port A (0x92)
    in   al, 0x92
    test al, 2
    jnz  .kbc
    or   al, 2
    and  al, 0xFE               ; never set bit 0, it resets the machine
    out  0x92, al
    call check_a20
    jnz  .ok

.kbc:
    ; 3) Keyboard controller: write output port with A20 bit set
    call .kbc_wait_in
    mov  al, 0xAD               ; disable keyboard
    out  0x64, al
    call .kbc_wait_in
    mov  al, 0xD0               ; read output port
    out  0x64, al
    call .kbc_wait_out
    in   al, 0x60
    push ax
    call .kbc_wait_in
    mov  al, 0xD1               ; write output port
    out  0x64, al
    call .kbc_wait_in
    pop  ax
    or   al, 2
    out  0x60, al
    call .kbc_wait_in
    mov  al, 0xAE               ; enable keyboard
    out  0x64, al
    call .kbc_wait_in
    call check_a20
    jnz  .ok
    stc
    ret
.ok:
    clc
    ret

.kbc_wait_in:                   ; wait until the input buffer is empty
    in   al, 0x64
    test al, 2
    jnz  .kbc_wait_in
    ret
.kbc_wait_out:                  ; wait until the output buffer is full
    in   al, 0x64
    test al, 1
    jz   .kbc_wait_out
    ret
