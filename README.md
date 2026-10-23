# x86_64 Bootloader — From Scratch

A step-by-step bootloader for x86_64 + BIOS, built progressively.
Targets: QEMU, Bochs. Features: FAT12, protected mode, long mode, ELF loader,
VGA, serial, keyboard, ATA, and a simple kernel shell.

---

## Project Structure

```
bootloader/
├── boot/           Assembly source — stage1, stage2, FAT12, GDT, paging
├── kernel/         C source — ELF loader, VGA, serial, keyboard, shell
├── include/        Shared headers — GDT, ELF, memory map, multiboot
├── docs/           Per-stage deep-dive documentation
├── scripts/        run_qemu.sh, gdbinit, bochs config
├── build/          All compiled output (gitignored)
└── Makefile
```

---

## Build Stages

| Stage | Branch of work | Status |
|-------|---------------|--------|
| 1 | Minimal boot sector (FAT12 BPB) | ✅ |
| 2 | Disk load via INT 13h, A20, E820 | ✅ |
| 3 | FAT12 + file search | ✅ |
| 4 | Protected mode (32-bit) | ✅ |
| 5 | Long mode (64-bit) | ✅ |
| 6 | ELF loader (C) | ✅ |
| 7 | Simple kernel + VGA | 🔜 |
| 8 | Shell + keyboard + ATA | 🔜 |

---

## Stage 1 — Minimal Boot Sector

### What happens at power-on

```
Power on
   │
   ▼
CPU starts at 0xFFFFFFF0 (reset vector, 16-bit real mode)
   │
   ▼
BIOS POST (Power-On Self-Test)
   │  checks RAM, detects devices, initialises IVT
   ▼
BIOS reads 512 bytes from LBA 0 into 0x0000:0x7C00
   │  verifies bytes 510–511 == 0x55 0xAA
   ▼
BIOS jumps to 0x0000:0x7C00
   │
   ▼
stage1.asm starts executing  ← WE ARE HERE (Stage 1)
   │  fixes segment registers, sets up stack
   │  prints banner via INT 10h
   │  loads Stage 2 via INT 13h
   ▼
jmp STAGE2_LOAD_SEG:0x0000
```

### Real-mode memory layout

```
0x00000 ┌─────────────────────────────┐
        │  IVT  (Interrupt Vector Table)│  1 KiB
0x00400 ├─────────────────────────────┤
        │  BDA  (BIOS Data Area)       │  256 bytes
0x00500 ├─────────────────────────────┤
        │                             │
        │  Stage 2 loaded here        │  ← 0x0500 (29 KiB free)
        │                             │
0x07C00 ├─────────────────────────────┤
        │  Stage 1 (this code, MBR)   │  512 bytes
0x07E00 ├─────────────────────────────┤
        │  Stack grows down from 7C00 │
        │  (free ~480 KiB)            │
0x80000 ├─────────────────────────────┤
        │  EBDA                       │
0xA0000 ├─────────────────────────────┤
        │  VGA memory (0xB8000 text)  │
0xC0000 ├─────────────────────────────┤
        │  Option ROMs / BIOS         │
0xFFFFF └─────────────────────────────┘
```

### Boot signature

The BIOS checks the **last two bytes** of the 512-byte sector:
- Byte 510 = `0x55`
- Byte 511 = `0xAA`

If missing → BIOS skips this device and tries the next boot option.

---

## Toolchain Setup

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install nasm gcc binutils qemu-system-x86 bochs bochs-x gdb xxd make
```

### Verify versions

```bash
nasm --version          # >= 2.15
qemu-system-x86_64 --version
gdb --version
```

---

## Build & Run (Stage 1)

```bash
# Build the disk image
make all

# Verify the 0x55AA boot signature
make verify

# Inspect the binary
make hexdump

# Run in QEMU
make run
```

### Expected output in QEMU window

```
============================
  x86_64 Bootloader v0.1
============================
  [STAGE1] Loading stage2...
  [STAGE1] Jumping to stage2
```

Then QEMU will **hang or triple-fault** — Stage 2 doesn't exist yet.
That is expected at Stage 1.

---

## Hex Dump Walkthrough

```bash
make hexdump
```

Expected output (truncated):

```
=== stage1.bin hex dump (first 64 bytes) ===
00000000: ea05 7c00 00fa 31c0 8ed8 8ec0 8ee0 8ee8  ..|...1.........
00000010: 8ed0 bc00 7cfb 8816 xxxx ...             .....|..........
                                  ^^^^
                              boot_drive variable offset

=== Boot signature (last 2 bytes, should be 55 AA) ===
000001fe: 55aa                                     U.
```

Key offsets in the binary:
| Offset | Content |
|--------|---------|
| `0x000` | `EA 05 7C 00 00` — far JMP to `0x0000:_init` |
| `0x005` | `FA` — CLI (disable interrupts) |
| `0x006` | `31 C0` — XOR AX, AX |
| `0x1FE` | `55 AA` — boot signature |

---

## Debugging (Stage 1)

### Method 1: QEMU monitor

```bash
make run
# In QEMU window, press: Ctrl-A  then  c
```

Useful QEMU monitor commands:
```
(qemu) info registers         # dump all CPU registers
(qemu) x /32xb 0x7c00        # hex dump 32 bytes at 0x7C00
(qemu) x /10i  0x7c00        # disassemble 10 instrs at 0x7C00
(qemu) info mem               # memory map
(qemu) q                      # quit
```

### Method 2: GDB + QEMU

Terminal 1:
```bash
make debug        # QEMU starts frozen, GDB stub on :1234
```

Terminal 2:
```bash
gdb -x scripts/gdbinit
(gdb) continue          # run until 0x7C00
(gdb) x/32xb 0x7c00    # inspect boot sector in RAM
(gdb) si                # step one instruction
(gdb) info registers    # dump registers
```

### Expected QEMU log (`build/qemu.log`)

After Stage 1 tries to jump to Stage 2 (which doesn't exist yet):
```
CPU Reset (CPU 0)
EAX=00000000 EBX=... ECX=... EDX=00000080
```
The `EDX=00000080` confirms BIOS passed drive 0x80 (first HDD) in DL. ✅

---

## Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| QEMU shows "Boot failed: not a bootable disk" | Missing 0x55AA | Run `make verify` |
| `stage1.bin is N bytes (expected 512)` | Data section overflow | Remove content from boot sector |
| Immediate reboot loop | Triple fault in stage1 | Use `make debug` + GDB to find crash |
| Black screen, no text | Wrong segment setup or bad INT 10h | Check CS=0x0000 and BH=0 |
| "Disk read failed!" on screen | INT 13h error, wrong CHS | Check `STAGE2_START_LBA` and drive number |

---
