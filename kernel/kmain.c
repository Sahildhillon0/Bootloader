/* kmain.c — kernel entry in C */
#include <stdint.h>
#include "bootinfo.h"

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void serial_puts(const char *s)
{
    for (; *s; s++) {
        while (!(inb(0x3F8 + 5) & 0x20))
            ;
        outb(0x3F8, (uint8_t)*s);
    }
}

void kmain(struct boot_info *bi)
{
    if (bi->magic == BOOT_INFO_MAGIC)
        serial_puts("[KERNEL] Hello from the 64-bit C kernel\r\n");
    else
        serial_puts("[KERNEL] bad boot_info magic\r\n");
}
