# Documentation

Everything about how the bootloader and kernel work, from the first byte
the BIOS loads to the shell.

## Start here

| Document | What it covers |
|----------|----------------|
| [call-tree.md](call-tree.md) | Call graphs for every part of the system, a tree of all functions by file, and the full call tree as text |
| [memory-graph.md](memory-graph.md) | Physical memory map, which function reads or writes which region, the boot timeline, disk-to-memory copies, page tables, kernel layout, `boot_info`, the interrupt stack frame |

## File and function reference

Every file, every function, what it does, its inputs and outputs, and who
calls it.

| Document | Covers |
|----------|--------|
| [reference/boot.md](reference/boot.md) | `boot/`: stage1, stage2 and all of its include files, the C ELF loader, the stage2 linker script |
| [reference/kernel.md](reference/kernel.md) | `kernel/`: entry, init, console, VGA, serial, GDT/IDT/ISRs, PIC, timer, keyboard, ATA, FAT12, shell |
| [reference/headers-build-scripts.md](reference/headers-build-scripts.md) | `include/`, the `Makefile`, `scripts/` (test driver, QEMU launcher, gdbinit), `rootfs/` |

## Stage deep dives

The *why* behind each stage, in the order they were built (one commit each).

| # | Document |
|---|----------|
| 1 | [01-boot-sector.md](01-boot-sector.md): BIOS entry, BPB, disk reads |
| 2 | [02-stage2.md](02-stage2.md): linking asm + C, serial, A20, E820 |
| 3 | [03-fat12.md](03-fat12.md): directory search, 12-bit cluster chains |
| 4 | [04-protected-mode.md](04-protected-mode.md): GDT, the switch, VGA output |
| 5 | [05-long-mode.md](05-long-mode.md): CPUID, page tables, the switch |
| 6 | [06-elf-loader.md](06-elf-loader.md): ELF segments, validation, `boot_info` |
| 7 | [07-kernel.md](07-kernel.md): freestanding C, console, IDT, PIC, PIT |
| 8 | [08-shell-and-drivers.md](08-shell-and-drivers.md): keyboard, ATA, FAT12, shell |

The diagrams use [Mermaid](https://mermaid.js.org/), which GitHub renders
inline.
