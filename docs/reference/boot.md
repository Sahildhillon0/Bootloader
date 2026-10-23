# Reference — `boot/`

Everything that runs before the kernel. Stage1 is a flat 512-byte binary;
stage2 is NASM (`elf64` object) plus one C file, linked into a flat binary
at `0x7E00` by `stage2.ld`.

Conventions in this file:
- **In / Out** are registers or memory the routine reads or produces.
- **Clobbers** lists registers changed on return. "nothing" means the
  routine saves and restores everything (`pusha`/`pushad`/push-pop).
- **Called by** lists every caller in the repo.

Mode legend: 🟦 16-bit real mode · 🟩 32-bit protected mode · 🟪 64-bit long mode

---

## `boot/stage1.asm` — the boot sector

**Use:** the 512 bytes the BIOS loads to `0x7C00`. They double as the FAT12
boot record (BPB at bytes 3–61). The job is to load stage2 (sectors 1–63)
to `0x7E00` and jump there. Includes `disk.inc`.

| Symbol | Mode | What it does |
|--------|------|--------------|
| `jmp short _start` + BPB fields (`bpb_oem` … `ebr_fs_type`) | data | FAT12 BIOS Parameter Block: 512 B/sector, 1 sector/cluster, **64 reserved sectors**, 2 FATs × 9 sectors, 224 root entries, 2880 sectors, 18 sectors/track, 2 heads. Read by mtools, by stage2 (at `0x7C00`) and by the kernel (over ATA). |
| `_start` | 🟦 | Far jump `0x0000:_init`, so `CS=0` whether the BIOS entered at `0000:7C00` or `07C0:0000`. |
| `_init` | 🟦 | Main flow: zero `DS/ES/SS`, stack at `0x7C00`, save `DL` to `boot_drive`, print the banner, `detect_ext`, loop `read_sector` over LBA 1 … `bpb_reserved-1` into `0x7E00`, then `jmp 0x0000:0x7E00` with `DL` = boot drive. |
| `disk_error` | 🟦 | Reached from `read_sector` after 5 failed attempts. Prints `[STAGE1] Disk read failed!` and falls into `halt`. |
| `halt` | 🟦 | `cli; hlt` loop. |
| `print_string` | 🟦 | **In:** `DS:SI` = NUL-terminated string. Writes each byte to COM1 (port `0x3F8`, no init) and to the screen via `INT 10h AH=0Eh`. **Clobbers:** nothing. **Called by:** `_init`, `disk_error`. |
| `boot_drive`, `use_ext` | data | Drive number from `DL`; 1 if INT 13h extensions work. Used by `disk.inc`. |
| `msg_*` | data | Banner and status strings. |
| `times 510-($-$$) db 0` / `dw 0xAA55` | data | Pad to 510 bytes and append the boot signature. |

---

## `boot/disk.inc` — BIOS disk reads (shared)

**Use:** reading sectors through INT 13h. Included by **both** stage1 and
stage2. The includer must define `boot_drive`, `use_ext`, `bpb_spt`,
`bpb_heads` and `disk_error`. Stage1 defines `bpb_*` as its BPB labels;
stage2 defines them as addresses inside `0x7C00`.

| Symbol | Mode | What it does |
|--------|------|--------------|
| `detect_ext` | 🟦 | `INT 13h AH=41h BX=55AAh`. Sets `[use_ext]=1` only if `CF=0`, `BX=AA55h` and `CX` bit 0 (AH=42h support). **Clobbers:** nothing. **Called by:** stage1 `_init`, stage2 `stage2_start`. |
| `read_sector` | 🟦 | **In:** `AX` = LBA, `ES:BX` = 512-byte buffer. LBA path: fills `dap` (count reset to 1 every try) and calls `AH=42h`. CHS path: sector = LBA mod SPT + 1, head = (LBA / SPT) mod heads, cylinder = (LBA / SPT) / heads, then `AH=02h`. Up to 5 attempts with `AH=00h` reset in between, then `jmp disk_error`. **Clobbers:** nothing. **Called by:** stage1 `_init`, `fat12_init`, `fat12_load_file`. |
| `dap` (`.count`, `.offset`, `.segment`, `.lba`) | data | 16-byte Disk Address Packet for `AH=42h`. |

---

## `boot/stage2.asm` — stage2 main flow

**Use:** the second stage. Runs in all three CPU modes, owns the
`boot_info` block, and calls the C ELF loader. Includes every `.inc` below.

| Symbol | Mode | What it does |
|--------|------|--------------|
| `stage2_start` | 🟦 | Entry at `0x7E00` (`.text16` is linked first). Resets segments and stack, zeroes `.bss` (`__bss_start`…`__bss_end` from `stage2.ld`), saves `DL`, then in order: `serial_init` → `enable_a20` → `read_e820` → `detect_ext` → `fat12_init` → `fat12_load_file("KERNEL  ELF" → 0x10000, max 320 KiB)` → reads the cursor (`INT 10h AH=03h`) into `vga_row` → `lgdt` → `CR0.PE=1` → `jmp 0x08:pm_entry`. Local failure labels `.a20_fail`, `.e820_fail`, `.kernel_error` jump to `fatal16` with a message. |
| `disk_error` | 🟦 | Required by `disk.inc`. Prints `[STAGE2] FATAL: disk read failed` via `fatal16`. |
| `fatal16` | 🟦 | **In:** `SI` = message. `print16` then falls into `halt16`. |
| `halt16` | 🟦 | `cli; hlt` loop. |
| `pm_entry` | 🟩 | Loads `DATA_SEL` (`0x10`) into every data segment, `ESP=0x7C00`, prints `[STAGE4]`, `check_long_mode` (fatal message if missing), `setup_paging`, then `jmp enter_long_mode`. |
| `lm_entry` | 🟪 | Reloads data segments, `RSP=0x80000`, prints `[STAGE5]`, calls `elf_load(0x10000, kernel_size, &boot_info)`. On `RAX≠0` prints the error string and halts. On success fills `boot_info` (magic, drive, mmap count/addr, `vga_row`/`vga_col`) and `jmp [boot_info.entry]` with `RDI = &boot_info`. |
| `boot_info` (`.magic` … `.entry`) | data | 48-byte structure handed to the kernel. Layout must match `include/bootinfo.h`. `.kernel_start`, `.kernel_end` and `.entry` are written by `elf_load`. |
| `kernel_name` | data | `"KERNEL  ELF"`, the 11-byte 8.3 name searched in the root directory. |
| `.bss`: `boot_drive`, `e820_count`, `use_ext`, `vga_row`, `vga_col`, `fat_root_lba`, `fat_root_secs`, `fat_data_lba`, `fat_file_size`, `fat_max_size`, `kernel_size` | data | State shared between the include files. |
| Constants `LOAD_SEG` (`0x1000`), `KERNEL_MAX` (`0x50000`), `STACK64_TOP` (`0x80000`), `BOOT_INFO_MAGIC` | equ | Kernel file buffer segment, its size limit, long-mode stack top, hand-off magic. |

---

## `boot/print16.inc` — real-mode console

**Use:** output before protected mode (BIOS screen + COM1).

| Symbol | Mode | What it does |
|--------|------|--------------|
| `serial_init` | 🟦 | COM1 → 115200 8N1: IER=0, DLAB=1, divisor 1, LCR=3, FIFO on (`0xC7`), MCR=DTR+RTS. **Clobbers:** nothing. **Called by:** `stage2_start`. |
| `putc16` | 🟦 | **In:** `AL` = char. Waits for LSR bit 5, writes COM1, then `INT 10h AH=0Eh`. **Clobbers:** nothing. **Called by:** `print16`, `print_hex16`. |
| `print16` | 🟦 | **In:** `DS:SI` = NUL-terminated string. Calls `putc16` per byte. **Called by:** `stage2_start`, `fatal16`. |
| `print_hex16` | 🟦 | **In:** `AX`. Prints 4 hex digits. **Called by:** `stage2_start` (drive number, E820 count, kernel size). |

## `boot/a20.inc` — the A20 gate

**Use:** make memory above 1 MiB addressable (the kernel lives there).

| Symbol | Mode | What it does |
|--------|------|--------------|
| `check_a20` | 🟦 | Writes `0x00` to `0000:0500` and `0xFF` to `FFFF:0510` (= `0x100500`). **Out:** `ZF=1` if the second write aliased the first (A20 off), `ZF=0` if A20 is on. Restores both bytes. **Clobbers:** flags only. **Called by:** `enable_a20`. |
| `enable_a20` | 🟦 | Tries, checking after each: already on → `INT 15h AX=2401h` → fast A20 (port `0x92` bit 1, never bit 0) → keyboard controller output port (`0xD0` read, `0xD1` write with bit 1). **Out:** `CF=0` on success. **Called by:** `stage2_start`. |
| `.kbc_wait_in` / `.kbc_wait_out` | 🟦 | Poll 8042 status bit 1 (input buffer empty) / bit 0 (output buffer full). |

## `boot/e820.inc` — BIOS memory map

| Symbol | Mode | What it does |
|--------|------|--------------|
| `read_e820` | 🟦 | Loops `INT 15h EAX=E820h EDX='SMAP' ECX=24` until `EBX=0` or `CF=1`. Stores 24-byte entries at `E820_MAP_ADDR` (`0x5000`), skips zero-length ones, at most `E820_MAX` (64). **Out:** `[e820_count]`, `CF=0` if at least one entry. **Clobbers:** nothing. **Called by:** `stage2_start`. |

## `boot/fat12.inc` — FAT12 in real mode

**Use:** find `KERNEL.ELF` in the root directory and load it, following the
cluster chain. Geometry is read from the BPB at `0x7C00` (`bpb_*` equates).

| Symbol | Mode | What it does |
|--------|------|--------------|
| `fat12_init` | 🟦 | Computes `fat_root_lba = reserved + fats × sec_per_fat`, `fat_root_secs = entries × 32 / 512`, `fat_data_lba = root_lba + root_secs`. Reads the root directory to `0x60000` and FAT #1 to `0x62000`. **Clobbers:** nothing. **Called by:** `stage2_start`. |
| `fat12_load_file` | 🟦 | **In:** `DS:SI` = 11-byte 8.3 name, `DX` = destination segment, `[fat_max_size]`. Scans root entries (skips attr `0x08`/`0x10`, so also long-name entries) with `repe cmpsb`. Refuses files larger than the limit. Then for each cluster: LBA = `data_lba + (N−2) × spc`, `read_sector` per sector with the segment advanced by `0x20`; next = 12-bit FAT entry at byte `N + N/2` (odd N: `>> 4`, even N: `& 0xFFF`); stops at `≥ 0xFF8`. **Out:** `CF=0, EAX=size`; `CF=1, EAX=0` not found; `CF=1, EAX=size` too large. **Called by:** `stage2_start`. |

## `boot/gdt.inc` — Global Descriptor Table

| Symbol | What it is |
|--------|------------|
| `gdt_start` | Null descriptor (selector `0x00`). |
| `gdt_code32` | `0x08`: base 0, limit 4 GiB, access `0x9A`, flags `0xC` (32-bit). |
| `gdt_data` | `0x10`: base 0, limit 4 GiB, access `0x92`. Used in protected and long mode. |
| `gdt_code64` | `0x18`: access `0x9A`, flags `0xA` (L=1). |
| `gdt_descriptor` | Limit + base for `lgdt` (padded to 10 bytes). |
| `CODE32_SEL`, `DATA_SEL`, `CODE64_SEL` | Selector equates used by the far jumps and segment loads. |

## `boot/print32.inc` — protected-mode console

| Symbol | Mode | What it does |
|--------|------|--------------|
| `putc32` | 🟩 | **In:** `AL`. Writes COM1, then VGA cell `[vga_row][vga_col]` at `0xB8000` with attribute `0x0A`. Handles `\r` (ignored on screen) and `\n`, wraps at 80 columns, scrolls at row 25 with `rep movsd`. The char is kept in `CL` because `EAX` is reused for the offset. **Clobbers:** nothing. **Called by:** `print32`. |
| `print32` | 🟩 | **In:** `ESI` = string. **Called by:** `pm_entry`. |
| `VGA_BASE`, `VGA_COLS`, `VGA_ROWS`, `VGA_ATTR` | equ | Also used by `print64.inc`. |

## `boot/paging.inc` — page tables and the long mode switch

| Symbol | Mode | What it does |
|--------|------|--------------|
| `check_long_mode` | 🟩 | Toggles `EFLAGS.ID` to prove CPUID exists, checks leaf `0x80000000 ≥ 0x80000001`, then `EDX` bit 29 of leaf `0x80000001`. **Out:** `CF=0` if 64-bit capable. **Called by:** `pm_entry`. |
| `setup_paging` | 🟩 | Zeroes `0x1000–0x3FFF`; PML4[0] → PDPT `0x2000`; PDPT[0] → PD `0x3000`; PD[i] = `i × 2 MiB | P | W | PS` for i = 0…511 (identity map of 1 GiB). **Clobbers:** nothing. **Called by:** `pm_entry`. |
| `enter_long_mode` | 🟩 → 🟪 | `CR4.PAE=1`, `CR3=0x1000`, `EFER.LME=1` (MSR `0xC0000080`), `CR0.PG=1`, `jmp 0x18:lm_entry`. Does not return. **Called by:** `pm_entry` (jump). |

## `boot/print64.inc` — long-mode console

| Symbol | Mode | What it does |
|--------|------|--------------|
| `putc64` | 🟪 | Same as `putc32`, saving registers with `push`/`pop` (no `pushad` in 64-bit mode). Shares `vga_row`/`vga_col`. **Called by:** `print64`. |
| `print64` | 🟪 | **In:** `RSI` = string. **Called by:** `lm_entry`. |

## `boot/elf_loader.c` — ELF64 loader (C, runs in stage2)

**Use:** turn the `KERNEL.ELF` file bytes into a loaded kernel image.
Compiled with the kernel's freestanding flags and linked into stage2.

| Function | What it does |
|----------|--------------|
| `static void copy_bytes(uint8_t *dst, const uint8_t *src, uint64_t n)` | Byte copy (no libc in stage2). **Called by:** `elf_load`. |
| `static void zero_bytes(uint8_t *dst, uint64_t n)` | Byte fill with 0 (`.bss`). **Called by:** `elf_load`. |
| `const char *elf_load(const uint8_t *image, uint64_t size, struct boot_info *bi)` | Validates: size ≥ header, magic, ELFCLASS64, little-endian, `ET_EXEC`, `EM_X86_64`, `e_phentsize`, program header table inside the file. For each `PT_LOAD`: `filesz ≤ memsz`, data inside the file, destination in `[1 MiB, 1 GiB)`; copies `filesz` bytes to `p_paddr` and zeroes the rest up to `memsz`. Checks the entry point is inside a loaded segment. **Out:** `NULL` and `bi->kernel_start/end/entry` set, or a static error string. **Called by:** `lm_entry` (System V ABI: `RDI`, `RSI`, `RDX` → `RAX`). |

## `boot/stage2.ld` — stage2 linker script

Places `.text16` (the real-mode entry) first at `0x7E00`, then C `.text`,
`.rodata`, `.data`, and a `NOLOAD` `.bss` bracketed by `__bss_start` /
`__bss_end`. `ASSERT`s that everything ends within 63 sectors
(`0x7E00 + 63×512`), discards `.eh_frame`/`.comment`/`.note*`, and outputs a
flat binary.
