/* kmain.c — kernel entry in C
 *
 * Called from entry.asm with the boot_info stage2 filled in. Brings up the
 * console, then the CPU tables, then interrupts.
 */
#include <stdint.h>
#include "bootinfo.h"
#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "pic.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

extern char __kernel_start[], __kernel_end[];

static const char *e820_type(uint32_t type)
{
    switch (type) {
    case E820_USABLE:   return "usable";
    case E820_RESERVED: return "reserved";
    case E820_ACPI:     return "ACPI reclaimable";
    case E820_NVS:      return "ACPI NVS";
    case E820_BAD:      return "bad";
    default:            return "unknown";
    }
}

static void print_memory_map(const struct boot_info *bi)
{
    const struct e820_entry *e = (const struct e820_entry *)bi->mmap_addr;
    uint64_t usable = 0;

    kprintf("[KERNEL] E820 memory map (%u entries):\n", bi->mmap_count);
    for (uint32_t i = 0; i < bi->mmap_count; i++) {
        kprintf("  %016lx - %016lx  %s\n", e[i].base,
                e[i].base + e[i].length - 1, e820_type(e[i].type));
        if (e[i].type == E820_USABLE)
            usable += e[i].length;
    }
    kprintf("[KERNEL] Usable RAM: %lu KiB\n", usable / 1024);
}

void kmain(struct boot_info *bi)
{
    serial_init();
    vga_init(bi->vga_row, bi->vga_col);

    if (bi->magic != BOOT_INFO_MAGIC) {
        kprintf("[KERNEL] bad boot_info magic %08x\n", bi->magic);
        halt_forever();
    }

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("[KERNEL] Hello from the 64-bit C kernel\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("[KERNEL] Loaded at %p - %p, boot drive 0x%02x\n",
            (void *)__kernel_start, (void *)__kernel_end, bi->boot_drive);
    print_memory_map(bi);

    gdt_init();
    idt_init();
    kprintf("[KERNEL] GDT and IDT loaded\n");
    __asm__ volatile ("int3");      /* proves the IDT works and returns */

    pic_init();
    timer_init();
    sti();
    timer_sleep(100);
    kprintf("[KERNEL] PIT running at %u Hz, %lu ticks so far\n",
            TIMER_HZ, timer_ticks());

    kprintf("[KERNEL] Idle.\n");
    for (;;)
        hlt();
}
