# x86_64 Bootloader Project

A minimal educational bootloader built completely from scratch for the x86_64 architecture.

This project starts with a 512-byte boot sector and gradually evolves into a multi-stage bootloader capable of entering 64-bit long mode and loading an ELF kernel.

The primary goal is to understand how real bootloaders work internally — from BIOS startup to protected mode, paging, long mode, and kernel loading.

---

# Project Goals

This bootloader is designed to teach:

- BIOS boot process
- x86 real mode programming
- Multi-stage bootloading
- Disk access using BIOS INT 13h
- FAT12 filesystem parsing
- GDT setup
- Protected mode transition
- Paging structures
- x86_64 long mode
- ELF64 kernel loading
- Low-level debugging using VGA and serial

Everything is written manually in Assembly and C with minimal abstractions.

---

# Development Roadmap

## Stage 1 — Boot Sector Scaffold
### Files
- `boot/stage1.asm`
- `Makefile`
- `scripts/run.sh`
- `README.md`

### Goals
- Create a valid 512-byte boot sector
- Print a boot message
- Boot successfully inside QEMU
- Establish project structure

---

## Stage 2 — Second Stage Loader
### Files
- `boot/stage2.asm`

### Goals
- Load additional sectors using BIOS INT 13h
- Transfer execution from stage1 → stage2
- Improve memory layout handling

---

## Stage 3 — FAT12 Parser
### Files
- `boot/fat12.asm`

### Goals
- Read FAT12 filesystem structures
- Search files from disk image
- Dynamically locate the kernel binary

---

## Stage 4 — Protected Mode Transition
### Files
- `boot/pm.asm`
- `include/gdt.inc`

### Goals
- Create and load a Global Descriptor Table (GDT)
- Enter 32-bit protected mode
- Prepare CPU state for modern execution

---

## Stage 5 — Long Mode + Paging
### Files
- `boot/lm.asm`
- `include/paging.inc`

### Goals
- Enable paging and PAE
- Enter x86_64 long mode
- Set up identity-mapped memory

---

## Stage 6 — ELF Loader
### Files
- `kernel/loader.c`
- `include/elf.h`

### Goals
- Parse ELF64 binaries
- Load kernel sections into memory
- Jump to kernel entry point

---

## Stage 7 — Minimal Kernel Runtime
### Files
- `kernel/kernel.c`
- `kernel/vga.c`

### Goals
- VGA text output
- Serial debugging support
- Basic kernel runtime environment

---

## Stage 8 — Shell + Drivers
### Files
- `kernel/shell.c`
- `kernel/ata.c`

### Goals
- Keyboard input
- ATA disk communication
- Tiny interactive shell

---

## Stage 9 — Documentation
### Files
- `docs/*.md`

### Goals
- Explain architecture decisions
- Document memory maps
- Describe boot flow and debugging techniques

---

# Planned Project Structure

```text
.
├── boot/
├── include/
├── kernel/
├── scripts/
├── docs/
├── Makefile
└── README.md
```

---

# Toolchain

Planned development environment:

* NASM
* x86_64-elf-gcc
* x86_64-elf-ld
* GNU Make
* QEMU

---

# Running

Planned commands:

```bash
make ./scripts/run.sh
```

---

# Philosophy

This project avoids external bootloaders and frameworks.

No GRUB.
No Limine.
No hidden runtime.

Only raw hardware, Assembly, C, and the x86 boot process.

---

# Status

🚧 Currently starting Stage 1 — minimal boot sector scaffold.

