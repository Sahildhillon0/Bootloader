; =============================================================================
; e820.inc — BIOS memory map (INT 15h, EAX=E820h)
; =============================================================================
; Each call returns one 24-byte entry: base (8), length (8), type (4), ACPI
; extended attributes (4). EBX is the continuation value; 0 means done.
; Entries go to E820_MAP_ADDR, the count to [e820_count].
; =============================================================================

E820_MAP_ADDR equ 0x5000
E820_MAX      equ 64
SMAP          equ 0x534D4150    ; 'SMAP'

; -----------------------------------------------------------------------------
; read_e820 — CF=0 on success
; -----------------------------------------------------------------------------
read_e820:
    pushad
    push es
    xor  ax, ax
    mov  es, ax
    mov  di, E820_MAP_ADDR
    xor  ebx, ebx
    mov  word [e820_count], 0
.next:
    mov  dword [es:di + 20], 1  ; ACPI 3.0: mark entry valid by default
    mov  eax, 0xE820
    mov  ecx, 24
    mov  edx, SMAP
    int  0x15
    jc   .end                   ; CF on a later call just means "no more"
    cmp  eax, SMAP
    jne  .fail
    jcxz .skip                  ; zero-length reply
    mov  eax, [es:di + 8]
    or   eax, [es:di + 12]
    jz   .skip                  ; zero-length region
    add  di, 24
    inc  word [e820_count]
    cmp  word [e820_count], E820_MAX
    jae  .end
.skip:
    test ebx, ebx
    jnz  .next
.end:
    cmp  word [e820_count], 0
    je   .fail
    pop  es
    popad
    clc
    ret
.fail:
    pop  es
    popad
    stc
    ret
