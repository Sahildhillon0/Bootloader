/* kernel.ld — the kernel is a static ELF64 loaded at 1 MiB (virtual = physical) */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : {
        *(.text.entry)              /* _start first, at 0x100000 */
        *(.text .text.*)
    }
    .rodata : ALIGN(4096) { *(.rodata .rodata.*) }
    .data   : ALIGN(4096) { *(.data .data.*) }
    .bss    : ALIGN(4096) { *(.bss .bss.*) *(COMMON) }

    __kernel_end = .;

    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
