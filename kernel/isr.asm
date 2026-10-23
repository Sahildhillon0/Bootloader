; =============================================================================
; isr.asm — interrupt entry stubs
; =============================================================================
; The CPU pushes SS, RSP, RFLAGS, CS, RIP (and an error code for some
; exceptions). Each stub pushes a dummy error code where the CPU does not,
; then the vector number, so every frame has the same shape. The common
; stub saves the general-purpose registers and calls isr_dispatch() with a
; pointer to the frame (struct regs in idt.h).
; =============================================================================

[BITS 64]
DEFAULT REL

extern isr_dispatch

%macro ISR_NOERR 1
isr_%1:
    push qword 0                ; dummy error code
    push qword %1
    jmp  isr_common
%endmacro

%macro ISR_ERR 1
isr_%1:
    push qword %1               ; CPU already pushed the error code
    jmp  isr_common
%endmacro

section .text

; Exceptions 0-31: 8, 10-14, 17, 21, 29, 30 push an error code
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; Hardware IRQs 0-15, remapped to vectors 32-47
%assign i 32
%rep 16
ISR_NOERR i
%assign i i + 1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov  rdi, rsp               ; struct regs *
    mov  rbx, rsp
    and  rsp, -16               ; SysV ABI: 16-byte aligned at the call
    cld
    call isr_dispatch
    mov  rsp, rbx

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    add  rsp, 16                ; vector + error code
    iretq

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dq isr_ %+ i
%assign i i + 1
%endrep
