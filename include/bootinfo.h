/* bootinfo.h — what the bootloader hands to the kernel (pointer in RDI)
 *
 * stage2.asm fills this structure byte by byte, so the layout is fixed:
 * any change here must be mirrored in the boot_info block in stage2.asm.
 */
#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

#define BOOT_INFO_MAGIC 0xB007B007u

/* E820 region types */
#define E820_USABLE   1
#define E820_RESERVED 2
#define E820_ACPI     3
#define E820_NVS      4
#define E820_BAD      5

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed));

struct boot_info {
    uint32_t magic;          /* +0  BOOT_INFO_MAGIC */
    uint8_t  boot_drive;     /* +4  BIOS drive number */
    uint8_t  vga_row;        /* +5  text cursor after the bootloader log */
    uint8_t  vga_col;        /* +6 */
    uint8_t  reserved0;      /* +7 */
    uint32_t mmap_count;     /* +8  number of E820 entries */
    uint32_t reserved1;      /* +12 */
    uint64_t mmap_addr;      /* +16 physical address of the E820 array */
    uint64_t kernel_start;   /* +24 lowest physical byte of the kernel */
    uint64_t kernel_end;     /* +32 one past the highest */
    uint64_t kernel_entry;   /* +40 ELF entry point */
} __attribute__((packed));

_Static_assert(sizeof(struct e820_entry) == 24, "E820 entry is 24 bytes");
_Static_assert(sizeof(struct boot_info) == 48, "boot_info layout is shared with stage2.asm");

#endif
