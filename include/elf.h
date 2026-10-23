/* elf.h — the subset of the ELF64 format the loader needs */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#define EI_NIDENT   16
#define ELFMAG0     0x7F
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_X86_64   62
#define PT_LOAD     1

typedef struct {
    uint8_t  e_ident[EI_NIDENT];  /* magic, class, endianness, version, ABI */
    uint16_t e_type;              /* ET_EXEC for a statically linked kernel */
    uint16_t e_machine;           /* EM_X86_64 */
    uint32_t e_version;
    uint64_t e_entry;             /* entry point (virtual = physical here) */
    uint64_t e_phoff;             /* program header table file offset */
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;         /* size of one program header */
    uint16_t e_phnum;             /* number of program headers */
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;              /* PT_LOAD segments get copied */
    uint32_t p_flags;
    uint64_t p_offset;            /* where the bytes are in the file */
    uint64_t p_vaddr;
    uint64_t p_paddr;             /* where they go in physical memory */
    uint64_t p_filesz;            /* bytes to copy */
    uint64_t p_memsz;             /* bytes to occupy (rest is zeroed: .bss) */
    uint64_t p_align;
} Elf64_Phdr;

#endif
