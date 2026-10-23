/* gdt.c — the kernel's own GDT
 *
 * The bootloader's GDT lives inside stage2's memory, which the kernel is
 * free to reuse. Load a copy we own and reload every segment register.
 * In long mode only the code descriptor's L bit and the DPL/present bits
 * matter; base and limit are ignored.
 */
#include <stdint.h>
#include "gdt.h"

static const uint64_t gdt[] __attribute__((aligned(8))) = {
    0x0000000000000000,     /* 0x00 null */
    0x00AF9A000000FFFF,     /* 0x08 kernel code: P, DPL0, exec/read, L=1 */
    0x00CF92000000FFFF,     /* 0x10 kernel data: P, DPL0, read/write */
};

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void gdt_init(void)
{
    struct gdt_ptr ptr = { sizeof(gdt) - 1, (uint64_t)gdt };

    __asm__ volatile (
        "lgdt %0\n\t"
        /* CS can only change through a far transfer: fake a far return */
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        :
        : "m"(ptr), "i"(KERNEL_CS), "i"(KERNEL_DS)
        : "rax", "memory");
}
