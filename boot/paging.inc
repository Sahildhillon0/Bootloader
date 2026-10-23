; =============================================================================
; paging.inc — long mode page tables and the switch to 64-bit (32-bit code)
; =============================================================================
; Long mode requires paging. We identity-map the first 1 GiB with 2 MiB pages,
; which needs only three tables:
;
;   PML4 @ 0x1000   entry 0   → PDPT
;   PDPT @ 0x2000   entry 0   → PD
;   PD   @ 0x3000   entry i   → physical i * 2 MiB  (PS=1, 512 entries)
;
; Entry flags: bit 0 present, bit 1 writable, bit 7 page size (2 MiB in a PD).
; The kernel keeps running on these tables after the hand-off.
; =============================================================================

[BITS 32]

PML4_ADDR equ 0x1000
PDPT_ADDR equ 0x2000
PD_ADDR   equ 0x3000

PAGE_PRESENT  equ 1 << 0
PAGE_WRITABLE equ 1 << 1
PAGE_HUGE     equ 1 << 7

EFER_MSR  equ 0xC0000080
EFER_LME  equ 1 << 8

; -----------------------------------------------------------------------------
; check_long_mode — CF=0 if the CPU supports 64-bit mode
; -----------------------------------------------------------------------------
check_long_mode:
    pushad
    ; CPUID itself: the ID bit (21) in EFLAGS must be toggleable
    pushfd
    pop  eax
    mov  ecx, eax
    xor  eax, 1 << 21
    push eax
    popfd
    pushfd
    pop  eax
    push ecx
    popfd
    cmp  eax, ecx
    je   .no

    mov  eax, 0x80000000        ; highest extended function
    cpuid
    cmp  eax, 0x80000001
    jb   .no
    mov  eax, 0x80000001
    cpuid
    test edx, 1 << 29           ; LM bit
    jz   .no
    popad
    clc
    ret
.no:
    popad
    stc
    ret

; -----------------------------------------------------------------------------
; setup_paging — build the identity map described above
; -----------------------------------------------------------------------------
setup_paging:
    pushad
    mov  edi, PML4_ADDR
    xor  eax, eax
    mov  ecx, 3 * 4096 / 4
    rep  stosd                  ; zero all three tables

    mov  dword [PML4_ADDR], PDPT_ADDR | PAGE_PRESENT | PAGE_WRITABLE
    mov  dword [PDPT_ADDR], PD_ADDR   | PAGE_PRESENT | PAGE_WRITABLE

    mov  edi, PD_ADDR
    mov  eax, PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE
    mov  ecx, 512
.fill:
    mov  [edi], eax             ; upper dword stays 0 (below 4 GiB)
    add  eax, 0x200000
    add  edi, 8
    loop .fill
    popad
    ret

; -----------------------------------------------------------------------------
; enter_long_mode — does not return; jumps to lm_entry in 64-bit mode
; -----------------------------------------------------------------------------
enter_long_mode:
    mov  eax, cr4
    or   eax, 1 << 5            ; CR4.PAE
    mov  cr4, eax

    mov  eax, PML4_ADDR
    mov  cr3, eax

    mov  ecx, EFER_MSR
    rdmsr
    or   eax, EFER_LME
    wrmsr

    mov  eax, cr0
    or   eax, 1 << 31           ; CR0.PG: long mode becomes active (compat mode)
    mov  cr0, eax

    jmp  CODE64_SEL:lm_entry    ; load a 64-bit CS: now in real 64-bit mode

[BITS 16]
