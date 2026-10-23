# Stage 1 — The boot sector

Source: [`boot/stage1.asm`](../boot/stage1.asm), [`boot/disk.inc`](../boot/disk.inc)

## What the BIOS gives us

At power-on the CPU starts in 16-bit real mode at the reset vector
`0xFFFFFFF0`. The BIOS runs its self-test, then reads **one sector** (512
bytes) from the boot device into `0x0000:0x7C00`. It checks that bytes 510
and 511 are `0x55 0xAA`, and jumps there with the boot drive number in `DL`
(`0x00` for the first floppy, `0x80` for the first hard disk).

Nothing else is guaranteed: segment registers, the stack and the direction
flag can hold anything. Some BIOSes jump to `0x07C0:0x0000` instead of
`0x0000:0x7C00`, which is the same physical address with a different `CS`.

## Doubling as a FAT12 boot record

The image is a FAT12 floppy, so the first sector must also be a valid FAT
boot record. The first three bytes are a short jump over the BIOS Parameter
Block (`EB 3C 90`), and bytes 3–61 hold the BPB:

```
00000000: eb3c 9042 4f4f 544c 4452 2000 0201 4000  .<.BOOTLDR ...@.
00000010: 02e0 0040 0bf0 0900 1200 0200 0000 0000  ...@............
00000020: 0000 0000 0000 2902 b0ad 1b42 4f4f 544c  ......)....BOOTL
00000030: 4f41 4445 5220 4641 5431 3220 2020 ea43  OADER FAT12   .C
```

| Offset | Field | Value |
|--------|-------|-------|
| `0x0B` | bytes per sector | 512 |
| `0x0D` | sectors per cluster | 1 |
| `0x0E` | reserved sectors | **64** (boot sector + 63 for stage2) |
| `0x10` | number of FATs | 2 |
| `0x11` | root entries | 224 |
| `0x13` | total sectors | 2880 |
| `0x15` | media | `0xF0` |
| `0x16` | sectors per FAT | 9 |
| `0x18` | sectors per track | 18 |
| `0x1A` | heads | 2 |

The trick is the reserved sector count. FAT reserves those sectors for the
boot loader, so stage2 lives in sectors 1–63 and the filesystem starts after
them. The volume stays readable by mtools, Linux and our own kernel.

The code starts at `0x3E` with a far jump, `EA 43 7C 00 00`, which pins `CS=0`.

## Steps

1. Far jump to `0x0000:_init` so every label is a plain offset.
2. `cli`, zero `DS`/`ES`/`SS`, put the stack at `0x7C00` (it grows down,
   away from our code), `sti`, `cld`.
3. Save `DL` as `boot_drive`.
4. Print the banner. `print_string` writes each byte to COM1 (`out 0x3F8`)
   and to the screen via `INT 10h, AH=0Eh`, so headless runs see it too.
5. `detect_ext`: `INT 13h, AH=41h, BX=55AAh`. If the BIOS answers `BX=AA55h`
   with bit 0 of `CX` set, LBA reads (`AH=42h`) are available.
6. Read sectors 1 … `bpb_reserved - 1` to `0x7E00`, one sector at a time.
7. Far jump to `0x0000:0x7E00` with `DL` still holding the boot drive.

## Reading the disk: `disk.inc`

`read_sector` takes an LBA in `AX` and a buffer in `ES:BX`:

- **LBA path**: fill a 16-byte Disk Address Packet (size, count, buffer,
  64-bit LBA) and call `AH=42h`. The count is rewritten before every try,
  because a failed call may store the number of sectors it managed (0).
- **CHS path** (floppies; SeaBIOS has no extensions for them):

  ```
  sector   = (LBA mod SPT) + 1
  head     = (LBA / SPT) mod HEADS
  cylinder = (LBA / SPT) / HEADS
  ```

  with cylinder bits 9–8 packed into the top of `CL`.

Each read is tried 5 times with a disk reset (`AH=00h`) in between. One
sector per call means a read never crosses a track (a CHS limit on many
BIOSes) or a 64 KiB DMA boundary.

`disk.inc` is shared: stage2 includes it too, with `bpb_spt`/`bpb_heads`
pointing into the BPB that stage1 left at `0x7C00`.

## Size budget

The boot sector holds 448 bytes of code and data (512 − 62 bytes of BPB − 2
bytes of signature). Stage1 currently uses 431 of the 510 available bytes. Everything
that does not need to be here lives in stage2.

## Try it

```bash
make hexdump          # first 64 bytes + signature
make run-floppy       # CHS path, DL=0x00
make run              # LBA path, DL=0x80
```
