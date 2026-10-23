# Stage 2 — Stage2 loader, A20 and the memory map

Source: [`boot/stage2.asm`](../boot/stage2.asm), [`boot/stage2.ld`](../boot/stage2.ld),
[`boot/a20.inc`](../boot/a20.inc), [`boot/e820.inc`](../boot/e820.inc),
[`boot/print16.inc`](../boot/print16.inc)

## Why a second stage

512 bytes is enough to load more code and nothing else. Stage2 has 63
sectors (31.5 KiB) and does the real work: A20, memory map, FAT12, the mode
switches and the ELF loader.

## Building asm and C into one flat binary

Stage2 is mostly assembly but includes a C file (the ELF loader, stage 6).
So instead of `nasm -f bin`, stage2 is assembled as an **ELF64 object** and
linked with `ld` using `stage2.ld`:

```
. = 0x7E00;
.text16 : { *(.text16) }      ← real-mode entry, must be first
.text   : { *(.text .text.*) } ← C code
.rodata, .data
.bss (NOLOAD)
ASSERT(__bss_end <= 0x7E00 + 63 * 512, "stage2 does not fit in 63 sectors")
OUTPUT_FORMAT(binary)
```

NASM is happy to emit `BITS 16` and `BITS 32` code into an ELF64 object;
the 16-bit absolute references become `R_X86_64_16` relocations that `ld`
resolves. That only works because every address is below 64 KiB, which the
`ASSERT` enforces. `.bss` is not stored in a flat binary, so `stage2_start`
zeroes it with `rep stosb` before anything else.

## Serial console

`serial_init` programs COM1 for 115200 baud 8N1: set DLAB in the line
control register, write divisor 1, clear DLAB, enable the FIFOs. `putc16`
waits for the transmit-holding-register-empty bit in the line status
register, then writes the byte to COM1 and to the screen.

## The A20 line

The 8086 had 20 address lines; `0xFFFF:0x0010` wrapped around to `0x00000`.
For compatibility, the PC gates address line 20 and boots with it disabled:
bit 20 of every physical address is forced to 0. The kernel loads at 1 MiB,
so A20 must be on.

`check_a20` writes different bytes to `0x0000:0x0500` and `0xFFFF:0x0510`
(physical `0x100500`). If the second write shows up at the first address,
memory wraps and A20 is off. `enable_a20` tries, checking after each:

1. BIOS `INT 15h, AX=2401h`
2. "Fast A20": set bit 1 of system control port `0x92` (never bit 0, it resets)
3. The keyboard controller: read its output port (`0xD0`), set bit 1, write
   it back (`0xD1`)

## The E820 memory map

`INT 15h, EAX=E820h, EDX='SMAP'` returns one region per call (24 bytes:
base, length, type, ACPI attributes) and a continuation value in `EBX`; 0
means done. Entries go to `0x5000`. Zero-length entries are skipped. Types:
1 usable, 2 reserved, 3 ACPI reclaimable, 4 ACPI NVS, 5 bad.

QEMU with 128 MiB reports 7 regions; the kernel prints them all and the
`mem` command shows them again.
