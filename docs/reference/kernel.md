# Reference — `kernel/`

The 64-bit kernel: a static ELF64 linked at `0x100000` (1 MiB), entered by
stage2 with `RDI = struct boot_info *`. Every file is listed with its
purpose, its functions, and who calls each function. The **Called by**
columns come from `gcc -fcallgraph-info`, plus the indirect calls through
function pointers (marked *via pointer*).

`static` functions are private to their file.

---

## `kernel/entry.asm` — entry point

**Use:** first kernel instruction (`.text.entry` is linked first, so `_start`
sits at `0x100000`).

| Symbol | What it does |
|--------|--------------|
| `_start` | `cli`, `RSP = stack_top`, `RBP = 0` (ends stack traces), `call kmain` (`RDI` still holds `boot_info`), then a `cli; hlt` loop if `kmain` ever returns. |
| `stack_bottom` / `stack_top` | 16 KiB kernel stack in `.bss` (already zeroed by the ELF loader). |

## `kernel/kernel.ld` — kernel linker script

ELF64 output, `ENTRY(_start)`, base `0x100000`. Order: `.text` (`.text.entry`
first), then page-aligned `.rodata`, `.data`, `.bss`. Exports
`__kernel_start` / `__kernel_end` (used by `kmain` and `cmd_mem`). Discards
`.eh_frame`, `.comment`, `.note*`.

---

## `kernel/kmain.c` — initialisation sequence

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static const char *e820_type(uint32_t type)` | Maps E820 type 1–5 to a name. | `print_memory_map` |
| `static void print_memory_map(const struct boot_info *bi)` | Prints every E820 entry from `bi->mmap_addr` and the total usable KiB. | `kmain` |
| `void kmain(struct boot_info *bi)` | Boot order: `serial_init` → `vga_init(bi->vga_row, bi->vga_col)` → check `BOOT_INFO_MAGIC` (halt if wrong) → banner → `print_memory_map` → `gdt_init` → `idt_init` → `int3` self-test → `pic_init` → `timer_init` → `sti` → `timer_sleep(100)` → `keyboard_init` → `ata_init` → `fat12_mount` → `shell_run` (never returns). | `_start` |

## `kernel/io.h` — port I/O and CPU helpers (header only)

All `static inline`, so they compile into their callers.

| Function | What it does |
|----------|--------------|
| `outb(port, v)` / `outw(port, v)` | Write a byte / word to an I/O port. |
| `inb(port)` / `inw(port)` | Read a byte / word from an I/O port. |
| `io_wait()` | Write to port `0x80` (≈1 µs delay between PIC commands). |
| `cli()` / `sti()` / `hlt()` | Interrupt flag and halt instructions. |
| `read_cr2()` | Faulting address after a page fault. |
| `halt_forever()` | `cli; hlt` loop, `noreturn`. |

## `kernel/string.c` / `string.h` — freestanding libc subset

gcc may emit calls to `memset`/`memcpy`/`memmove`/`memcmp` on its own, so
these names must exist.

| Function | What it does | Called by |
|----------|--------------|-----------|
| `void *memset(void *dst, int c, size_t n)` | Fill memory. | `name_to_83`, compiler |
| `void *memcpy(void *dst, const void *src, size_t n)` | Copy memory (no overlap). | `fat12_read`, compiler |
| `void *memmove(void *dst, const void *src, size_t n)` | Copy memory (overlap-safe). | compiler (library) |
| `int memcmp(const void *a, const void *b, size_t n)` | Compare memory. | `fat12_read` |
| `size_t strlen(const char *s)` | String length. | library |
| `int strcmp(const char *a, const char *b)` | Compare strings. | `shell_run` |
| `int strncmp(const char *a, const char *b, size_t n)` | Compare at most `n` chars. | library |

"library" means available for new code but not called today.

---

## `kernel/serial.c` / `serial.h` — COM1 driver

| Function | What it does | Called by |
|----------|--------------|-----------|
| `void serial_init(void)` | 115200 baud 8N1, FIFOs on, no UART interrupts (polled). | `kmain` |
| `void serial_putc(char c)` | Wait for LSR bit 5 (transmitter empty), write the byte. | `console_putc` |
| `int serial_getc(void)` | Return a byte if LSR bit 0 (data ready) is set, else `-1`. | `input_getc` |

## `kernel/vga.c` / `vga.h` — 80×25 text mode

State: `cur_row`, `cur_col`, `attr` (colour). The buffer is
`volatile uint16_t *` at `0xB8000` (low byte char, high byte attribute).

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static void update_cursor(void)` | Move the hardware cursor (CRTC registers `0x0E`/`0x0F` via `0x3D4`/`0x3D5`). | `vga_init`, `vga_clear`, `vga_putc` |
| `static void scroll(void)` | Move rows 1–24 up one, blank row 24. | `vga_putc` |
| `void vga_init(uint8_t row, uint8_t col)` | Start at the bootloader's last cursor position. | `kmain` |
| `void vga_clear(void)` | Blank the screen, cursor home. | `cmd_clear`, `read_line` (Ctrl-L) |
| `void vga_set_color(enum vga_color fg, enum vga_color bg)` | Set the attribute for later characters. | `kmain`, `isr_dispatch`, `shell_run` |
| `void vga_putc(char c)` | Draw one char, handling `\n` `\r` `\b` `\t`, wrap and scroll. | `console_putc` |

`enum vga_color`: the 16 VGA palette colours (`VGA_BLACK` … `VGA_WHITE`).

## `kernel/console.c` / `console.h` — console and printf

| Function | What it does | Called by |
|----------|--------------|-----------|
| `void console_putc(char c)` | Send one byte to COM1 (`\n` → `\r\n`) and to VGA. The single output funnel of the kernel. | `console_puts`, `pad`, `put_number`, `kvprintf`, `cmd_cat`, `read_line` |
| `void console_puts(const char *s)` | `console_putc` per byte. | `kvprintf`, `read_line`, `shell_run` |
| `static int pad(int n, char c)` | Print `c` `n` times (field width padding). | `kvprintf`, `put_number` |
| `static int put_number(uint64_t v, unsigned base, bool upper, bool negative, int width, bool zero, bool left)` | Format an integer in base 10/16 with sign, width, zero or left padding. | `kvprintf` |
| `int kvprintf(const char *fmt, va_list ap)` | printf core: `%c %s %d %i %u %x %X %p %%`, flags `0` `-`, width, `l`/`ll`/`z`. | `kprintf` |
| `int kprintf(const char *fmt, ...)` | Variadic wrapper, checked by gcc's `format(printf)` attribute. | almost everything |

---

## `kernel/gdt.c` / `gdt.h` — kernel GDT

| Symbol | What it does | Called by |
|--------|--------------|-----------|
| `gdt[]` | null, `0x08` 64-bit code, `0x10` data. | — |
| `void gdt_init(void)` | `lgdt`, reload `CS` with a far return (`push 0x08; push label; lretq`), reload `DS/ES/FS/GS/SS` with `0x10`. | `kmain` |
| `KERNEL_CS`, `KERNEL_DS` | Selector constants. | `idt.c` |

## `kernel/isr.asm` — interrupt entry stubs

| Symbol | What it does |
|--------|--------------|
| `ISR_NOERR n` / `ISR_ERR n` (macros) | Generate `isr_n`: push a dummy error code (if the CPU did not), push the vector, `jmp isr_common`. |
| `isr_0` … `isr_31` | CPU exceptions (8, 10–14, 17, 21, 29, 30 have error codes). |
| `isr_32` … `isr_47` | IRQ 0–15 after the PIC remap. |
| `isr_common` | Push the 15 general-purpose registers, `RDI = RSP` (the `struct regs *`), align the stack, `call isr_dispatch`, restore, drop vector + error code, `iretq`. |
| `isr_stub_table` | 48 stub addresses, read by `idt_init`. |

## `kernel/idt.c` / `idt.h` — IDT and dispatch

`struct regs` mirrors the stack frame built by `isr_common` (r15…rax,
vector, error, then the CPU's rip/cs/rflags/rsp/ss).

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static void set_gate(int vec, uint64_t handler)` | Fill a 16-byte gate: selector `0x08`, type `0x8E` (present, ring 0, interrupt gate). | `idt_init` |
| `void idt_init(void)` | Install all 48 stubs, `lidt`. | `kmain` |
| `void irq_install(int irq, irq_handler_t handler)` | Register a handler in `irq_handlers[]` and unmask the line. | `timer_init`, `keyboard_init` |
| `static void dump_regs(const struct regs *r)` | Print RIP/CS/RFLAGS/RSP/SS/ERR and the main registers. | `isr_dispatch` |
| `void isr_dispatch(struct regs *r)` | Vector ≥ 32: ignore spurious, call `irq_handlers[irq]` *via pointer* (`timer_irq`, `keyboard_irq`), `pic_eoi`. Vector 3: print and resume. Other exceptions: red screen, name, `CR2` for #PF, `dump_regs`, `halt_forever`. | `isr_common` |

## `kernel/pic.c` / `pic.h` — 8259A PIC pair

| Function | What it does | Called by |
|----------|--------------|-----------|
| `void pic_init(void)` | ICW1–ICW4: remap to vectors 32–47, cascade on IRQ 2, mask everything except IRQ 2. | `kmain` |
| `void pic_unmask(int irq)` | Clear the line's bit in the master or slave mask register. | `irq_install` |
| `void pic_mask(int irq)` | Set the bit. | library |
| `void pic_eoi(int irq)` | End of interrupt to the slave (IRQ ≥ 8) and the master. | `isr_dispatch` |
| `bool pic_is_spurious(int irq)` | For IRQ 7/15, read the in-service register; spurious if the bit is clear (IRQ 15 still EOIs the master). | `isr_dispatch` |

## `kernel/timer.c` / `timer.h` — PIT system tick

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static void timer_irq(struct regs *r)` | `ticks++`. | `isr_dispatch` *via pointer* |
| `void timer_init(void)` | PIT channel 0, mode 3, divisor `1193182 / 100`; `irq_install(0, timer_irq)`. | `kmain` |
| `uint64_t timer_ticks(void)` | Ticks since `timer_init`. | `kmain`, `cmd_uptime` |
| `void timer_sleep(uint64_t ms)` | `hlt` until enough ticks pass (rounded up to 10 ms). | `kmain` |

---

## `kernel/keyboard.c` / `keyboard.h` — PS/2 keyboard and input queue

State: modifier flags (`shift`, `ctrl`, `caps`, `extended`), a 128-byte ring
buffer (`buf`, `head`, `tail`), scancode set 1 tables `keymap` /
`keymap_shift`.

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static void push(char c)` | Append to the ring buffer, dropping keys when it is full. | `keyboard_irq` |
| `static void keyboard_irq(struct regs *r)` | Read port `0x60`; track `0xE0` prefix, Shift, Ctrl, Caps; translate make codes to ASCII; Ctrl+letter → control code; `push`. | `isr_dispatch` *via pointer* |
| `void keyboard_init(void)` | Drain stale bytes from the 8042, `irq_install(1, keyboard_irq)`. | `kmain` |
| `int input_getc(void)` | Blocking read: ring buffer first (with interrupts briefly off), then `serial_getc` (DEL → `\b`), else `hlt` until the next interrupt. | `read_line` |

## `kernel/ata.c` / `ata.h` — ATA PIO driver

State: `static struct ata_info info` (present, model, serial, sector count).

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static void delay400(void)` | Read the alternate status 4 times (≈400 ns). | `ata_init`, `ata_read` |
| `static bool wait_not_busy(void)` | Poll until BSY clears (bounded loop). | `ata_init`, `ata_read` |
| `static bool wait_drq(void)` | Poll until DRQ sets; fail on ERR/DF or timeout. | `ata_init`, `ata_read` |
| `static void ata_string(char *dst, const uint16_t *src, int words)` | Byte-swap an IDENTIFY string and trim trailing spaces. | `ata_init` |
| `bool ata_init(void)` | Set nIEN, select master, IDENTIFY (`0xEC`); reject a missing drive or ATAPI/SATA signature; read model (words 27–46), serial (10–19), LBA28 size (60–61). | `kmain` |
| `const struct ata_info *ata_info(void)` | Read-only access to the drive info. | `kmain`, `cmd_ls`, `cmd_disk` |
| `bool ata_read(uint32_t lba, uint8_t count, void *buf)` | LBA28 READ SECTORS (`0x20`): range check, program the task file, per sector `wait_drq` + 256 × `inw`. | `fat12_mount`, `fat12_read` |

## `kernel/fat12.c` / `fat12.h` — read-only FAT12

State: cached `fat[]` (up to 12 sectors) and `root[]` (up to 32 sectors),
`root_entries`, `data_lba`, `sectors_per_cluster`, `mounted`.

| Function | What it does | Called by |
|----------|--------------|-----------|
| `bool fat12_mount(void)` | Read sector 0, validate the BPB and `55 AA`, compute the root and data LBAs, cache FAT #1 and the root directory. | `kmain` |
| `static uint16_t fat_next(uint16_t cluster)` | 12-bit FAT entry for `cluster` (word at `N + N/2`, odd → `>> 4`, even → `& 0xFFF`). | `fat12_read` |
| `static void name_from_83(char *dst, const char *src)` | `HELLO   TXT` → `HELLO.TXT`. | `fat12_list` |
| `static bool name_to_83(char *dst, const char *src)` | `hello.txt` → `HELLO   TXT`; false if not a valid 8.3 name. | `fat12_read` |
| `static const struct dirent *entry(int i)` | Pointer to root entry `i`. | `fat12_list`, `fat12_read` |
| `static bool is_file(const struct dirent *d)` | Not deleted, not a long-name entry, not a volume label or directory. | `fat12_list`, `fat12_read` |
| `void fat12_list(void (*fn)(const struct fat12_dirent *e))` | Call `fn` for each file (*via pointer*: `print_dirent`). | `cmd_ls` |
| `int64_t fat12_read(const char *name, void *buf, uint32_t max)` | Find the file, follow the chain with `ata_read`, copy up to `max` bytes. **Out:** bytes read, or `-1`. | `cmd_cat` |

## `kernel/shell.c` / `shell.h` — command shell

State: `boot` (the `boot_info`), `file_buf[64 KiB]` for `cat`, and the
`commands[]` table (name, help text, handler).

| Function | What it does | Called by |
|----------|--------------|-----------|
| `static void cmd_help(int, char **)` | List `commands[]`. | `shell_run` *via pointer* |
| `static void cmd_clear(int, char **)` | `vga_clear`. | `shell_run` *via pointer* |
| `static void cmd_echo(int, char **)` | Print the arguments. | `shell_run` *via pointer* |
| `static void cmd_mem(int, char **)` | Kernel range (`__kernel_start/end`) and the E820 map. | `shell_run` *via pointer* |
| `static void print_dirent(const struct fat12_dirent *)` | One `ls` line. | `fat12_list` *via pointer* |
| `static void cmd_ls(int, char **)` | `fat12_list(print_dirent)`, or explain that there is no ATA disk. | `shell_run` *via pointer* |
| `static void cmd_cat(int, char **)` | `fat12_read` into `file_buf`, print with non-printables as `.`. | `shell_run` *via pointer* |
| `static void cmd_disk(int, char **)` | ATA model, serial, size, BIOS boot drive. | `shell_run` *via pointer* |
| `static void cmd_uptime(int, char **)` | Seconds and ticks from `timer_ticks`. | `shell_run` *via pointer* |
| `static void cmd_crash(int, char **)` | Read `0x40000000` (past the identity map) → page fault demo. | `shell_run` *via pointer* |
| `static void cmd_reboot(int, char **)` | Wait for the 8042, write `0xFE` to port `0x64` (CPU reset). | `shell_run` *via pointer* |
| `static void cmd_shutdown(int, char **)` | ACPI power-off writes for QEMU (`0x604`), Bochs (`0xB004`), VirtualBox (`0x4004`); halt if none worked. | `shell_run` *via pointer* |
| `static int read_line(char *line, int max)` | Echoing line editor: Enter, backspace, Ctrl-C, Ctrl-L. | `shell_run` |
| `static int split(char *line, char **argv)` | Cut on spaces/tabs into at most 8 arguments. | `shell_run` |
| `void shell_run(const struct boot_info *bi)` | Prompt `boot> `, `read_line`, `split`, look up `argv[0]` with `strcmp`, call the handler. Never returns. | `kmain` |
