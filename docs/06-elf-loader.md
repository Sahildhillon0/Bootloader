# Stage 6 — The ELF loader and the hand-off

Source: [`boot/elf_loader.c`](../boot/elf_loader.c), [`include/elf.h`](../include/elf.h),
[`include/bootinfo.h`](../include/bootinfo.h), [`kernel/kernel.ld`](../kernel/kernel.ld)

## The kernel as an ELF file

The kernel is linked as a static ELF64 executable at 1 MiB:

```
$ readelf -lW build/kernel.elf
  Type  Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg
  LOAD  0x001000 0x0000000000100000 0x0000000000100000 0x00276f 0x00276f R E   .text
  LOAD  0x004000 0x0000000000103000 0x0000000000103000 0x001040 0x001040 R     .rodata
  LOAD  0x006000 0x0000000000105000 0x0000000000105000 0x000001 0x01b9d2 RW    .data + .bss
```

A `PT_LOAD` program header says: copy `p_filesz` bytes from `p_offset` in
the file to `p_paddr`, then zero up to `p_memsz`. The zeroed tail is `.bss`.
Section headers, symbols and debug info are irrelevant to loading, so the
copy written to the disk is stripped (`objcopy --strip-debug`), and
`build/kernel.elf` keeps the symbols for GDB.

## Running C inside stage2

`elf_loader.c` is compiled with the same flags as the kernel and linked into
stage2. Stage2 calls it from long mode with the System V calling convention:

```nasm
mov  edi, 0x10000           ; RDI: image
mov  esi, [kernel_size]     ; RSI: size
lea  rdx, [boot_info]       ; RDX: struct boot_info *
call elf_load               ; RAX: NULL or an error string
```

The stack at `0x80000` is 16-byte aligned before the `call`, as the ABI
requires.

## Trust nothing in the file

`elf_load` checks every field before using it, so a corrupt file produces a
message instead of a copy over the loader:

- file large enough for the header; magic `7F 'E' 'L' 'F'`
- 64-bit, little-endian, `ET_EXEC`, `EM_X86_64`
- program header table size and position inside the file
- each `PT_LOAD`: `filesz ≤ memsz`, data inside the file, destination inside
  `[1 MiB, 1 GiB)`: above the loader and the file buffer, inside the
  identity map
- at least one segment, and the entry point inside a loaded segment

Each check has its own error string; stage2 prints it after
`[STAGE6] FATAL: bad kernel ELF:`.

## boot_info

The kernel gets one pointer, in `RDI`, to this structure (filled by stage2;
the layout is fixed by `_Static_assert` in `bootinfo.h`):

| Offset | Field | |
|--------|-------|-|
| 0 | `magic` | `0xB007B007` |
| 4 | `boot_drive` | BIOS drive number |
| 5, 6 | `vga_row`, `vga_col` | where the bootloader log ends |
| 8 | `mmap_count` | E820 entries |
| 16 | `mmap_addr` | `0x5000` |
| 24, 32 | `kernel_start`, `kernel_end` | physical range used by the segments |
| 40 | `kernel_entry` | `e_entry` |

Then `jmp [boot_info.entry]`. `kernel/entry.asm` sets its own 16 KiB stack
(inside `.bss`, already zeroed by the loader) and calls `kmain(bi)`.
