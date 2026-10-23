; =============================================================================
; gdt.inc — Global Descriptor Table for the protected and long mode switch
; =============================================================================
; Segment descriptor (8 bytes):
;   limit 15..0 | base 15..0 | base 23..16 | access | flags:limit 19..16 | base 31..24
;
; Access byte: P DPL(2) S E DC RW A
;   0x9A = present, ring 0, code, executable, readable
;   0x92 = present, ring 0, data, writable
; Flags nibble: G DB L -
;   0xC = 4 KiB granularity, 32-bit       (protected mode)
;   0xA = 4 KiB granularity, long mode    (L=1 requires DB=0)
; =============================================================================

align 8
gdt_start:
    dq 0                        ; 0x00: null descriptor (required)

gdt_code32:                     ; 0x08: 32-bit code, base 0, limit 4 GiB
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00

gdt_data:                       ; 0x10: data, base 0, limit 4 GiB
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00

gdt_code64:                     ; 0x18: 64-bit code (base/limit ignored)
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xAF, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; limit
    dd gdt_start                ; base (linear)
    dd 0                        ; upper half, read by LGDT in long mode

CODE32_SEL equ gdt_code32 - gdt_start
DATA_SEL   equ gdt_data   - gdt_start
CODE64_SEL equ gdt_code64 - gdt_start
