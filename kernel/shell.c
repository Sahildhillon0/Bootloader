/* shell.c — a small command shell
 *
 * Reads a line from input_getc() (PS/2 keyboard or COM1), splits it into
 * whitespace-separated arguments and dispatches to a command table.
 */
#include <stdint.h>
#include "ata.h"
#include "console.h"
#include "fat12.h"
#include "io.h"
#include "keyboard.h"
#include "shell.h"
#include "string.h"
#include "timer.h"
#include "vga.h"

#define LINE_MAX 128
#define ARGS_MAX 8
#define PROMPT   "boot> "

extern char __kernel_start[], __kernel_end[];

static const struct boot_info *boot;
static uint8_t file_buf[64 * 1024];

struct command {
    const char *name;
    const char *help;
    void (*fn)(int argc, char **argv);
};

static const struct command commands[];

/* ---------------------------------------------------------------- commands */

static void cmd_help(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("Commands:\n");
    for (const struct command *c = commands; c->name; c++)
        kprintf("  %-10s %s\n", c->name, c->help);
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc, (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        kprintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    kprintf("\n");
}

static void cmd_mem(int argc, char **argv)
{
    (void)argc, (void)argv;
    const struct e820_entry *e = (const struct e820_entry *)boot->mmap_addr;
    uint64_t usable = 0;

    kprintf("Kernel: %p - %p (%lu KiB)\n", (void *)__kernel_start,
            (void *)__kernel_end,
            (uint64_t)(__kernel_end - __kernel_start) / 1024);
    for (uint32_t i = 0; i < boot->mmap_count; i++) {
        kprintf("  %016lx  %10lu KiB  %s\n", e[i].base, e[i].length / 1024,
                e[i].type == E820_USABLE ? "usable" : "reserved");
        if (e[i].type == E820_USABLE)
            usable += e[i].length;
    }
    kprintf("Usable: %lu MiB\n", usable >> 20);
}

static void print_dirent(const struct fat12_dirent *e)
{
    kprintf("  %-12s %8u bytes  cluster %u\n", e->name, e->size, e->cluster);
}

static void cmd_ls(int argc, char **argv)
{
    (void)argc, (void)argv;
    if (!ata_info()->present) {
        kprintf("ls: no ATA disk (booted from floppy?)\n");
        return;
    }
    fat12_list(print_dirent);
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("usage: cat FILE\n");
        return;
    }
    int64_t n = fat12_read(argv[1], file_buf, sizeof(file_buf));
    if (n < 0) {
        kprintf("cat: %s: not found\n", argv[1]);
        return;
    }
    for (int64_t i = 0; i < n; i++) {
        char c = file_buf[i];
        console_putc((c >= 32 && c < 127) || c == '\n' || c == '\t' ? c : '.');
    }
    if (n > 0 && file_buf[n - 1] != '\n')
        kprintf("\n");
}

static void cmd_disk(int argc, char **argv)
{
    (void)argc, (void)argv;
    const struct ata_info *d = ata_info();
    if (!d->present) {
        kprintf("No ATA disk on the primary master.\n");
        return;
    }
    kprintf("Model:   %s\nSerial:  %s\nSectors: %u (%u KiB)\n", d->model,
            d->serial, d->sectors, d->sectors / 2);
    kprintf("Boot drive (BIOS): 0x%02x\n", boot->boot_drive);
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc, (void)argv;
    uint64_t t = timer_ticks();
    kprintf("Up %lu.%02lu s (%lu ticks at %u Hz)\n", t / TIMER_HZ,
            t % TIMER_HZ * 100 / TIMER_HZ, t, TIMER_HZ);
}

static void cmd_crash(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("Reading 0x40000000, just past the 1 GiB identity map...\n");
    volatile uint64_t *p = (volatile uint64_t *)0x40000000;
    (void)*p;
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("Rebooting...\n");
    cli();
    while (inb(0x64) & 2)           /* 8042 input buffer must be empty */
        ;
    outb(0x64, 0xFE);               /* pulse the CPU reset line */
    halt_forever();
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("Powering off.\n");
    outw(0x604, 0x2000);            /* QEMU (ACPI PM1a control) */
    outw(0xB004, 0x2000);           /* Bochs, older QEMU */
    outw(0x4004, 0x3400);           /* VirtualBox */
    kprintf("ACPI power-off did not work; halting.\n");
    halt_forever();
}

static const struct command commands[] = {
    { "help",     "list commands",                  cmd_help },
    { "clear",    "clear the screen",               cmd_clear },
    { "echo",     "print the arguments",            cmd_echo },
    { "mem",      "show the E820 memory map",       cmd_mem },
    { "ls",       "list files on the FAT12 disk",   cmd_ls },
    { "cat",      "print a file: cat HELLO.TXT",    cmd_cat },
    { "disk",     "show ATA drive information",     cmd_disk },
    { "uptime",   "time since the timer started",   cmd_uptime },
    { "crash",    "trigger a page fault (demo)",    cmd_crash },
    { "reboot",   "reset the machine",              cmd_reboot },
    { "shutdown", "power off (QEMU/Bochs/VBox)",    cmd_shutdown },
    { 0, 0, 0 },
};

/* ------------------------------------------------------------------ input */

static int read_line(char *line, int max)
{
    int len = 0;
    for (;;) {
        int c = input_getc();
        if (c == '\r' || c == '\n') {
            console_putc('\n');
            line[len] = 0;
            return len;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                console_puts("\b \b");
            }
        } else if (c == 3) {                    /* Ctrl-C: drop the line */
            console_puts("^C\n");
            len = 0;
            line[0] = 0;
            return 0;
        } else if (c == 12) {                   /* Ctrl-L: clear screen */
            vga_clear();
            line[len] = 0;
            console_puts(PROMPT);
            console_puts(line);
        } else if (c >= 32 && c < 127 && len < max - 1) {
            line[len++] = (char)c;
            console_putc((char)c);
        }
    }
}

static int split(char *line, char **argv)
{
    int argc = 0;
    while (*line && argc < ARGS_MAX) {
        while (*line == ' ' || *line == '\t')
            *line++ = 0;
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t')
            line++;
    }
    return argc;
}

void shell_run(const struct boot_info *bi)
{
    char line[LINE_MAX];
    char *argv[ARGS_MAX];

    boot = bi;
    kprintf("Type 'help' for a list of commands.\n");
    for (;;) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        console_puts(PROMPT);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

        read_line(line, sizeof(line));
        int argc = split(line, argv);
        if (argc == 0)
            continue;

        const struct command *c;
        for (c = commands; c->name; c++)
            if (!strcmp(c->name, argv[0]))
                break;
        if (c->name)
            c->fn(argc, argv);
        else
            kprintf("%s: unknown command (try 'help')\n", argv[0]);
    }
}
