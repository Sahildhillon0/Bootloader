/* =============================================================================
 * elf_loader.c — load the kernel ELF image (runs in stage2, 64-bit long mode)
 * =============================================================================
 * Stage2 has already read KERNEL.ELF from the FAT12 volume into a buffer at
 * 0x10000. This copies every PT_LOAD segment to its physical address and
 * zero-fills the part of the segment that is not in the file (.bss).
 *
 * Every field is checked against the file size before it is used: a corrupt
 * image must produce an error message, not a wild copy over the loader.
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "elf.h"
#include "bootinfo.h"

/* Segments must land in [1 MiB, 1 GiB): above the loader, the ELF buffer and
 * the BIOS areas, and inside the identity map built by paging.inc. */
#define LOAD_MIN 0x100000ull
#define LOAD_MAX 0x40000000ull

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint64_t n)
{
    while (n--)
        *dst++ = *src++;
}

static void zero_bytes(uint8_t *dst, uint64_t n)
{
    while (n--)
        *dst++ = 0;
}

/* Returns NULL on success (bi->kernel_* filled in) or an error message. */
const char *elf_load(const uint8_t *image, uint64_t size, struct boot_info *bi)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)image;

    if (size < sizeof(Elf64_Ehdr))
        return "file smaller than an ELF header";
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3)
        return "bad ELF magic";
    if (eh->e_ident[4] != ELFCLASS64)
        return "not a 64-bit ELF";
    if (eh->e_ident[5] != ELFDATA2LSB)
        return "not little-endian";
    if (eh->e_type != ET_EXEC)
        return "not an executable (ET_EXEC)";
    if (eh->e_machine != EM_X86_64)
        return "not an x86_64 ELF";
    if (eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_phnum == 0)
        return "bad program header table";
    if (eh->e_phoff > size ||
        (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > size - eh->e_phoff)
        return "program headers past end of file";

    uint64_t lo = UINT64_MAX, hi = 0;
    const Elf64_Phdr *ph = (const Elf64_Phdr *)(image + eh->e_phoff);

    for (uint16_t i = 0; i < eh->e_phnum; i++, ph++) {
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return "segment file size exceeds memory size";
        if (ph->p_offset > size || ph->p_filesz > size - ph->p_offset)
            return "segment data past end of file";
        if (ph->p_paddr < LOAD_MIN || ph->p_paddr >= LOAD_MAX ||
            ph->p_memsz > LOAD_MAX - ph->p_paddr)
            return "segment outside 1 MiB .. 1 GiB";

        uint8_t *dst = (uint8_t *)(uintptr_t)ph->p_paddr;
        copy_bytes(dst, image + ph->p_offset, ph->p_filesz);
        zero_bytes(dst + ph->p_filesz, ph->p_memsz - ph->p_filesz);

        if (ph->p_paddr < lo)
            lo = ph->p_paddr;
        if (ph->p_paddr + ph->p_memsz > hi)
            hi = ph->p_paddr + ph->p_memsz;
    }

    if (hi == 0)
        return "no loadable segments";
    if (eh->e_entry < lo || eh->e_entry >= hi)
        return "entry point outside the loaded segments";

    bi->kernel_start = lo;
    bi->kernel_end   = hi;
    bi->kernel_entry = eh->e_entry;
    return NULL;
}
