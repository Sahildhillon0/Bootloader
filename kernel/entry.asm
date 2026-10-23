; =============================================================================
; entry.asm — kernel entry point (64-bit long mode)
; =============================================================================
; Jumped to by stage2 with RDI = struct boot_info *. Interrupts are off,
; paging is on (first 1 GiB identity mapped), .bss is already zeroed by the
; ELF loader. All that is left is a stack of our own.
; =============================================================================

[BITS 64]
DEFAULT REL

extern kmain

section .text.entry
global _start
_start:
    cli
    lea  rsp, [stack_top]
    xor  rbp, rbp               ; terminate stack traces
    call kmain                  ; RDI still holds boot_info
.hang:
    cli
    hlt
    jmp  .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
