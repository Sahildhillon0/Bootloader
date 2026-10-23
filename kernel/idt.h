/* idt.h — interrupt descriptor table and dispatch */
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Stack frame built by isr.asm, lowest address first */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;     /* pushed by the CPU */
};

#define IRQ_BASE 32

typedef void (*irq_handler_t)(struct regs *r);

void idt_init(void);
void irq_install(int irq, irq_handler_t handler);

#endif
