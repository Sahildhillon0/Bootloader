/* stage2.ld — link stage2 as a flat binary loaded at 0x7E00 */
OUTPUT_FORMAT(binary)
ENTRY(stage2_start)

SECTIONS
{
    . = 0x7E00;

    /* Real-mode entry must come first: stage1 jumps to 0x7E00 */
    .text16 : { *(.text16) }
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    }

    /* Everything must stay inside the 63 reserved sectors */
    ASSERT(__bss_end <= 0x7E00 + 63 * 512, "stage2 does not fit in 63 sectors")

    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
