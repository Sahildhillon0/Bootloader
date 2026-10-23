# Reference — headers, build and scripts

## `include/` — shared between stage2 and the kernel

### `include/bootinfo.h`

**Use:** the contract between the bootloader and the kernel. Stage2 fills
the structure byte by byte in assembly (`boot_info` in `stage2.asm`), so the
layout is pinned with `_Static_assert`.

| Item | Meaning |
|------|---------|
| `BOOT_INFO_MAGIC` | `0xB007B007`; `kmain` refuses to run without it. |
| `E820_USABLE` … `E820_BAD` | E820 region types 1–5. |
| `struct e820_entry` | `base`, `length`, `type`, `acpi`: 24 bytes, packed, the exact BIOS format. |
| `struct boot_info` | `magic`(+0) `boot_drive`(+4) `vga_row`(+5) `vga_col`(+6) `mmap_count`(+8) `mmap_addr`(+16) `kernel_start`(+24) `kernel_end`(+32) `kernel_entry`(+40): 48 bytes. |

Used by: `boot/elf_loader.c`, `kernel/kmain.c`, `kernel/shell.c`.

### `include/elf.h`

**Use:** the subset of ELF64 the loader needs.

| Item | Meaning |
|------|---------|
| `ELFMAG0–3`, `ELFCLASS64`, `ELFDATA2LSB`, `ET_EXEC`, `EM_X86_64`, `PT_LOAD` | Values checked by `elf_load`. |
| `Elf64_Ehdr` | File header: identity bytes, type, machine, entry, program header offset/size/count. |
| `Elf64_Phdr` | Program header: type, file offset, physical address, file size, memory size. |

Used by: `boot/elf_loader.c`.

---

## `Makefile`

| Variable / rule | What it does |
|-----------------|--------------|
| `NASM_FLAGS` | `-i boot/ -w+all -w-reloc` (absolute relocations are intended). |
| `CFLAGS` | Freestanding 64-bit C: no libc, no red zone, no SSE, no PIC, no stack protector, `-fno-tree-loop-distribute-patterns`. Shared by `elf_loader.c` and the kernel. |
| `QEMU_DRIVE` / `QEMU_COMMON` / `QEMU_FLAGS` | Image as IDE primary master, 128 MiB, `-no-reboot`, `isa-debug-exit`, serial on stdio, interrupt trace to `build/qemu.log`. |
| `$(STAGE1_BIN)` | `nasm -f bin`, then fails unless the result is exactly 512 bytes. |
| `build/stage2.o`, `build/elf_loader.o`, `$(STAGE2_BIN)` | NASM `elf64` + gcc, linked by `boot/stage2.ld` into a flat binary. |
| `build/kernel/%.o`, `%.asm.o`, `$(KERNEL_ELF)` | Every `kernel/*.c` and `kernel/*.asm`, linked by `kernel/kernel.ld` (`-z max-page-size=0x1000`). |
| `$(DISK_IMG)` | 2880 zero sectors → stage1 at LBA 0 → stage2 at LBA 1 → `F0 FF FF` into both FATs → `mcopy` the debug-stripped kernel as `KERNEL.ELF` and every file in `rootfs/` (uppercased). |
| `run`, `run-headless`, `run-floppy`, `debug` | QEMU variants (window, no window, floppy drive `0x00`, GDB stub on `:1234`). |
| `test` | `python3 scripts/test_boot.py build/boot.img`. |
| `hexdump`, `verify` | First 64 bytes + signature; signature check + `mdir`. |
| `clean`, `help` | Remove `build/`; list targets. |

## `scripts/`

### `scripts/test_boot.py`

**Use:** end-to-end test behind `make test`.

| Item | What it does |
|------|--------------|
| `class Machine` | One QEMU process: serial on stdin/stdout pipes, monitor on a UNIX socket. |
| `Machine._reader` | Background thread collecting serial output (CR stripped). |
| `Machine.text` / `wait_for(s, timeout, start)` | Snapshot of the output; wait until `s` appears after offset `start`. |
| `Machine.send(line)` | Type a line over serial, 5 ms per byte (stays under the 16-byte UART FIFO). |
| `Machine.run(cmd)` | Send a shell command and return its output up to the next prompt. |
| `Machine.monitor(cmd)` / `_drain` | Send a QEMU monitor command and read the reply. |
| `Machine.screen()` | `xp /2000hx 0xb8000`, decode the low bytes into 25 lines of text. |
| `Machine.sendkeys(text)` | Type on the emulated PS/2 keyboard with `sendkey`. |
| `Machine.close()` | Kill QEMU and close the socket. |
| `report(ok, what)` | Print PASS/FAIL and count failures. |
| `BOOT_MARKERS`, `SCREEN_MARKERS`, `SHELL_STEPS` | Expected serial lines, on-screen text, and command/output pairs. |
| Main script | Run 1: boot markers, screen, shell, keyboard, shutdown. Run 2: `crash` → page fault + `CR2`. Run 3: floppy boot without an ATA disk. Writes `build/test_serial.log` and `build/test_vga.txt`; exits 1 on any failure. |

### `scripts/run_qemu.sh`

**Use:** a standalone QEMU launcher. `check_deps` checks for `qemu-system-x86_64`, `nasm`
and `make`; `build_if_needed` runs `make all` if the image is missing. Modes: default (window + monitor
on stdio, serial to `build/serial.log`), `--debug` (frozen, GDB on `:1234`),
`--nographic` (no window, serial on stdio). Helpers `info`, `warn`, `error`
print coloured messages.

### `scripts/gdbinit`

**Use:** `gdb -x scripts/gdbinit`. Connects to `localhost:1234`, sets
`i8086`, breaks at `0x7C00`, displays `CS`/`IP`/`EAX`/`ESP`. Comments show
how to switch to `i386` / `i386:x86-64` and load `build/kernel.elf` symbols.

## `rootfs/`

Every file here is copied into the FAT12 root directory with an uppercased
name. `hello.txt` → `HELLO.TXT`, which `cat hello.txt` and the tests read.

## Top-level files

| File | Use |
|------|-----|
| `README.md` | Overview, quick start, layout, memory map, targets, debugging. |
| `CLAUDE.md` | Working rules for AI assistants in this repo (test before commit, owner-only commits). |
| `.gitignore` | Ignores `build/` and binary outputs. |
