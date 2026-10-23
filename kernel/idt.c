/* idt.c — 64-bit IDT, exception reporting, IRQ dispatch
 *
 * Each IDT gate is 16 bytes: the handler address split in three parts,
 * the code selector, and a type byte (0x8E = present, DPL 0, interrupt
 * gate, so IF is cleared on entry).
 */
#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "pic.h"
#include "vga.h"

struct idt_gate {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern const uint64_t isr_stub_table[48];

static struct idt_gate idt[256] __attribute__((aligned(16)));
static irq_handler_t irq_handlers[16];

static const char *const exception_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow",
    "BOUND range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS",
    "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved", "x87 floating-point", "Alignment check",
    "Machine check", "SIMD floating-point", "Virtualization",
    "Control protection", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor injection", "VMM communication",
    "Security", "Reserved",
};

static void set_gate(int vec, uint64_t handler)
{
    idt[vec] = (struct idt_gate){
        .offset_lo  = handler & 0xFFFF,
        .selector   = KERNEL_CS,
        .ist        = 0,
        .type       = 0x8E,
        .offset_mid = (handler >> 16) & 0xFFFF,
        .offset_hi  = handler >> 32,
    };
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++)
        set_gate(i, isr_stub_table[i]);

    struct idt_ptr ptr = { sizeof(idt) - 1, (uint64_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(ptr));
}

void irq_install(int irq, irq_handler_t handler)
{
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

static void dump_regs(const struct regs *r)
{
    kprintf("  RIP=%016lx  CS=%04lx  RFLAGS=%016lx\n", r->rip, r->cs, r->rflags);
    kprintf("  RSP=%016lx  SS=%04lx  ERR=%016lx\n", r->rsp, r->ss, r->error);
    kprintf("  RAX=%016lx RBX=%016lx RCX=%016lx\n", r->rax, r->rbx, r->rcx);
    kprintf("  RDX=%016lx RSI=%016lx RDI=%016lx\n", r->rdx, r->rsi, r->rdi);
    kprintf("  RBP=%016lx R8 =%016lx R9 =%016lx\n", r->rbp, r->r8, r->r9);
}

void isr_dispatch(struct regs *r)
{
    if (r->vector >= IRQ_BASE) {
        int irq = r->vector - IRQ_BASE;
        /* A spurious IRQ 7/15 has no ISR bit set and must not get an EOI */
        if (pic_is_spurious(irq))
            return;
        if (irq_handlers[irq])
            irq_handlers[irq](r);
        pic_eoi(irq);
        return;
    }

    if (r->vector == 3) {
        /* Breakpoint is a trap: report it and carry on */
        kprintf("[IDT] Breakpoint at RIP=%016lx, resuming\n", r->rip);
        return;
    }

    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n*** CPU EXCEPTION %lu: %s ***\n", r->vector,
            exception_names[r->vector]);
    if (r->vector == 14)
        kprintf("  CR2=%016lx (faulting address)\n", read_cr2());
    dump_regs(r);
    kprintf("System halted.\n");
    halt_forever();
}
