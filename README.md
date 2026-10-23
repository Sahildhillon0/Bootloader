# x86_64 Bootloader — From Scratch

A BIOS bootloader for x86_64, built stage by stage, that boots a small 64-bit
C kernel with a shell. Everything is written from scratch: no GRUB, no libc,
no cross-compiler.

```
BIOS → stage1 (512 B, FAT12 boot sector)
     → stage2 (real mode → protected mode → long mode, C ELF loader)
     → KERNEL.ELF (64-bit C: VGA, serial, IDT, timer, keyboard, ATA, FAT12, shell)
```

Runs in QEMU. The disk image is a normal 1.44 MB FAT12 volume: you can
`mdir`/`mcopy` files in and out of it, and the kernel reads them back over ATA.

---

## Quick start

```bash
# Arch
sudo pacman -S nasm qemu-system-x86 qemu-ui-gtk mtools tinyxxd
# Ubuntu / Debian
sudo apt install nasm gcc qemu-system-x86 mtools xxd

make            # build build/boot.img
make run        # boot it (QEMU window, serial console on this terminal)
make test       # automated end-to-end test (38 checks)
```

Then, at the `boot>` prompt:

```
boot> ls
  KERNEL.ELF      29680 bytes  cluster 2
  HELLO.TXT          42 bytes  cluster 60
boot> cat hello.txt
Hello from HELLO.TXT on the FAT12 volume!
boot> disk
Model:   QEMU HARDDISK
Serial:  QM00001
Sectors: 2880 (1440 KiB)
Boot drive (BIOS): 0x80
boot> uptime
Up 4.24 s (424 ticks at 100 Hz)
```

Type in the QEMU window (PS/2 keyboard) or in the terminal (COM1): both feed
the same shell.

---

## Build stages

Each stage is one commit in the history and has a deep-dive doc.

| Stage | What it adds | Doc | Status |
|-------|--------------|-----|--------|
| 1 | Boot sector with a FAT12 BPB, loads stage2 | [docs/01-boot-sector.md](docs/01-boot-sector.md) | ✅ |
| 2 | Disk load via INT 13h, A20, E820 memory map | [docs/02-stage2.md](docs/02-stage2.md) | ✅ |
| 3 | FAT12 root directory search + cluster chain loader | [docs/03-fat12.md](docs/03-fat12.md) | ✅ |
| 4 | GDT, 32-bit protected mode | [docs/04-protected-mode.md](docs/04-protected-mode.md) | ✅ |
| 5 | Page tables, 64-bit long mode | [docs/05-long-mode.md](docs/05-long-mode.md) | ✅ |
| 6 | ELF64 loader in C, kernel hand-off | [docs/06-elf-loader.md](docs/06-elf-loader.md) | ✅ |
| 7 | C kernel: VGA, serial, GDT/IDT, PIC, PIT | [docs/07-kernel.md](docs/07-kernel.md) | ✅ |
| 8 | PS/2 keyboard, ATA PIO, FAT12 driver, shell | [docs/08-shell-and-drivers.md](docs/08-shell-and-drivers.md) | ✅ |

---

## Boot log

```
x86_64 Bootloader v1.0
[STAGE1] Loading stage2...
[STAGE1] Jumping to stage2
[STAGE2] Running at 0x7E00, boot drive 0x0080
[STAGE2] A20 enabled
[STAGE2] E820 memory map: 0x0007 entries
[STAGE3] Loaded KERNEL.ELF from FAT12: 0x000073F0 bytes
[STAGE2] Entering protected mode
[STAGE4] 32-bit protected mode, flat 4 GiB segments
[STAGE4] Page tables built, entering long mode
[STAGE5] 64-bit long mode, first 1 GiB identity mapped
[STAGE6] ELF segments loaded, jumping to kernel
[KERNEL] Hello from the 64-bit C kernel
[KERNEL] Loaded at 0x0000000000100000 - 0x00000000001209d2, boot drive 0x80
[KERNEL] E820 memory map (7 entries):
  ...
[KERNEL] GDT and IDT loaded
[IDT] Breakpoint at RIP=0000000000101952, resuming
[KERNEL] PIT running at 100 Hz, 11 ticks so far
[KERNEL] PS/2 keyboard on IRQ 1
[KERNEL] ATA disk: QEMU HARDDISK, 2880 sectors
[KERNEL] FAT12 volume mounted
Type 'help' for a list of commands.
boot>
```

Every line goes to both the VGA screen and COM1, from the first BIOS-assisted
print in stage1 to the kernel shell.

---

## Project structure

```
boot/
  stage1.asm       boot sector: BPB, loads sectors 1..63 to 0x7E00
  stage2.asm       stage2 main flow, boot_info hand-off
  disk.inc         INT 13h reads (LBA extensions, CHS fallback, retries)
  print16.inc      real-mode output (BIOS teletype + COM1)
  a20.inc          A20 gate: BIOS, port 0x92, keyboard controller
  e820.inc         BIOS memory map
  fat12.inc        FAT12 root search + cluster chain loader (real mode)
  gdt.inc          GDT: 32-bit code/data, 64-bit code
  print32.inc      protected-mode output (VGA memory + COM1)
  paging.inc       CPUID check, identity-mapped page tables, long mode switch
  print64.inc      long-mode output
  elf_loader.c     ELF64 loader, runs inside stage2 in long mode
  stage2.ld        links stage2 (asm + C) into a flat binary at 0x7E00
kernel/
  entry.asm        _start: stack, call kmain
  kmain.c          init sequence
  console.c        kprintf, VGA + serial fan-out
  vga.c serial.c   text mode and COM1 drivers
  gdt.c idt.c      CPU tables, exception reporting, IRQ dispatch
  isr.asm          interrupt entry stubs
  pic.c timer.c    8259 PIC remap, 8254 PIT at 100 Hz
  keyboard.c       PS/2 keyboard + shared input queue
  ata.c fat12.c    ATA PIO driver, read-only FAT12
  shell.c          command shell
  string.c io.h    freestanding helpers
  kernel.ld        static ELF64 at 1 MiB
include/
  bootinfo.h       boot_info layout shared by stage2 and the kernel
  elf.h            ELF64 structures
rootfs/            files copied into the FAT12 image (HELLO.TXT)
scripts/
  test_boot.py     end-to-end test driver
  run_qemu.sh      QEMU launcher (normal / --debug / --nographic)
  gdbinit          GDB setup for the QEMU stub
docs/              per-stage deep dives
```

---

## Disk layout

The image is a 1.44 MB floppy-format FAT12 volume (2880 sectors). The BPB in
sector 0 reserves 64 sectors, so stage2 lives in front of the filesystem and
the filesystem stays valid.

| LBA | Contents |
|-----|----------|
| 0 | stage1 + BPB (boot signature `55 AA` at offset 510) |
| 1 – 63 | stage2 (up to 31.5 KiB) |
| 64 – 72 | FAT #1 |
| 73 – 81 | FAT #2 |
| 82 – 95 | root directory (224 entries) |
| 96 – 2879 | data clusters: `KERNEL.ELF`, `HELLO.TXT`, … |

```bash
make verify                                # boot signature + FAT12 listing
mdir  -i build/boot.img ::                 # list files
mcopy -i build/boot.img notes.txt ::       # add a file (or drop it in rootfs/)
```

## Memory map

| Physical | Used for |
|----------|----------|
| `0x00000 – 0x004FF` | IVT + BIOS Data Area |
| `0x01000 – 0x03FFF` | page tables: PML4, PDPT, PD (kept by the kernel) |
| `0x05000 – 0x055FF` | E820 memory map (up to 64 entries) |
| `0x07C00 – 0x07DFF` | stage1, with the BPB stage2 reads |
| `0x07E00 – 0x0FDFF` | stage2 |
| `0x10000 – 0x5FFFF` | `KERNEL.ELF` file buffer (max 320 KiB) |
| `0x60000 – 0x63FFF` | FAT12 root directory + FAT buffers (stage2) |
| `0x70000 – 0x7FFFF` | stage2 long-mode stack (top at `0x80000`) |
| `0xB8000` | VGA text buffer |
| `0x100000 –` | kernel: `.text`, `.rodata`, `.data`, `.bss` (16 KiB stack inside) |

---

## Make targets

| Target | What it does |
|--------|--------------|
| `make` / `make all` | Build `build/boot.img` |
| `make run` | QEMU with a window; serial console on stdio |
| `make run-headless` | QEMU without a window; serial console only |
| `make run-floppy` | Boot the image as a floppy (drive `0x00`, CHS reads, no ATA disk) |
| `make debug` | QEMU frozen at reset with a GDB stub on `:1234` |
| `make test` | End-to-end test (see below) |
| `make hexdump` | First 64 bytes and boot signature of `stage1.bin` |
| `make verify` | Check the boot signature and list the FAT12 root |
| `make clean` | Remove `build/` |

## Testing

`make test` runs `scripts/test_boot.py`, which drives QEMU through its
serial port and monitor socket:

1. waits for every boot marker on COM1
2. reads the VGA text buffer at `0xB8000` and checks what is on screen
3. runs shell commands over serial (`help`, `ls`, `cat`, `disk`, `mem`, `uptime`, …)
4. types `echo kbd` on the emulated PS/2 keyboard with `sendkey`
5. runs `shutdown` and expects QEMU to power off
6. reboots, runs `crash`, and checks the page fault report and `CR2`
7. boots the image as a floppy and checks the shell copes without a disk

Serial output is saved to `build/test_serial.log`, the screen to `build/test_vga.txt`.

---

## Debugging

```bash
make debug                    # terminal 1: QEMU waits for GDB
gdb -x scripts/gdbinit        # terminal 2: breaks at 0x7C00
```

`build/kernel.elf` keeps its debug info (the copy on the disk is stripped),
so once in long mode you can load kernel symbols:

```
(gdb) set architecture i386:x86-64
(gdb) symbol-file build/kernel.elf
(gdb) break kmain
```

QEMU's interrupt and reset trace goes to `build/qemu.log` with `make run`.
A triple fault shows up there as `CPU Reset`, with the register state just
before it.

## Common issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Boot failed: not a bootable disk` | Missing `55 AA` | `make verify` |
| `stage1.bin is N bytes (expected 512)` | Boot sector code too large | Move code to stage2 |
| `stage2 does not fit in 63 sectors` | stage2.ld size assert | Raise `bpb_reserved` in stage1.asm and `RESERVED_SECTORS` in the Makefile together |
| `[STAGE1] Disk read failed!` | INT 13h error | Check drive number and BPB geometry |
| `[STAGE3] FATAL: KERNEL.ELF not found` | File missing from the image | `make clean all`, check `mdir -i build/boot.img ::` |
| `[STAGE6] FATAL: bad kernel ELF: …` | Kernel ELF rejected | The message names the check that failed |
| Reboot loop / QEMU exits at once | Triple fault | `make run`, then read `build/qemu.log` |
| `ls: no ATA disk` | Booted as a floppy | Use `make run` (IDE disk) |
