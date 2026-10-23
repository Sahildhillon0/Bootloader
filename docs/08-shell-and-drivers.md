# Stage 8 — Keyboard, ATA, FAT12 and the shell

Source: [`kernel/keyboard.c`](../kernel/keyboard.c), [`kernel/ata.c`](../kernel/ata.c),
[`kernel/fat12.c`](../kernel/fat12.c), [`kernel/shell.c`](../kernel/shell.c)

## PS/2 keyboard

The BIOS has already set up the 8042 controller, so the driver only drains
stale bytes and installs an IRQ 1 handler. Each interrupt delivers one
scancode at port `0x60` (set 1): the make code on press, `make | 0x80` on
release, `0xE0` before extended keys. The handler tracks Shift, Ctrl and
Caps Lock, translates with two 128-entry tables (normal and shifted) and
pushes characters into a ring buffer. Ctrl+letter becomes the control code
(Ctrl-C = 3, Ctrl-L = 12).

## One input queue, two sources

`input_getc` pops from the keyboard ring buffer and, if it is empty, polls
the COM1 line status register for a received byte. When neither has
anything it executes `hlt`; the 100 Hz timer wakes it to poll again. That is
why the same shell answers in the QEMU window and in the terminal.

## ATA PIO

The primary channel's task file is at `0x1F0–0x1F7`, device control at
`0x3F6`. The driver sets `nIEN` (no drive interrupts) and polls.

**IDENTIFY** (`0xEC`): select the master (`0xA0`), zero the count and LBA
registers, send the command. Status 0 means no drive; non-zero LBA mid/high
after BSY clears means ATAPI/SATA, not ATA. Otherwise read 256 words: the
model is words 27–46, the serial number words 10–19 (both byte-swapped
strings), and the LBA28 sector count words 60–61.

**READ SECTORS** (`0x20`), LBA28:

```
0x1F6 ← 0xE0 | LBA[27:24]     (LBA mode, master)
0x1F2 ← sector count
0x1F3 ← LBA[7:0]   0x1F4 ← LBA[15:8]   0x1F5 ← LBA[23:16]
0x1F7 ← 0x20
per sector: wait BSY=0, DRQ=1 → 256 × inw(0x1F0)
```

Reading the alternate status register four times gives the 400 ns delay
the drive needs after a select or before checking status.

## FAT12 in the kernel

`fat12.c` is the same algorithm as `boot/fat12.inc`, now in C on top of ATA.
`fat12_mount` reads sector 0, validates the BPB and signature, and caches
the FAT and root directory. `fat12_read` converts `hello.txt` to
`HELLO   TXT`, finds the entry and follows the 12-bit chain into the
caller's buffer.

## The shell

`read_line` echoes printable characters, handles backspace (`\b`, or `DEL`
from terminals), Ctrl-C (drop the line) and Ctrl-L (clear the screen).
`split` cuts the line into at most 8 arguments, and a table maps names to
handlers:

| Command | Does |
|---------|------|
| `help` | list commands |
| `clear` | clear the screen |
| `echo ARGS` | print the arguments |
| `mem` | kernel range and the E820 map |
| `ls` | FAT12 root directory |
| `cat FILE` | print a file (non-printable bytes as `.`) |
| `disk` | ATA model, serial, size |
| `uptime` | ticks since the PIT started |
| `crash` | read `0x40000000`, past the identity map: page fault demo |
| `reboot` | pulse the CPU reset line through the 8042 (`0xFE` to port `0x64`) |
| `shutdown` | ACPI power-off: `0x604` (QEMU), `0xB004` (Bochs), `0x4004` (VirtualBox) |

## Test coverage

`scripts/test_boot.py` exercises all of this in QEMU: commands over serial,
`echo kbd` typed with the monitor's `sendkey` to cover the PS/2 path, a
`shutdown` that must power QEMU off, a `crash` that must report vector 14
with `CR2=0x40000000`, and a floppy boot where `ls` must explain that there
is no ATA disk.
