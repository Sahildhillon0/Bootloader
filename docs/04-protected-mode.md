# Stage 4 — Protected mode

Source: [`boot/gdt.inc`](../boot/gdt.inc), [`boot/print32.inc`](../boot/print32.inc)

## Segment descriptors

In protected mode a segment register holds a **selector**, an index into
the Global Descriptor Table. Each 8-byte descriptor packs a base, a limit,
an access byte and flags:

```
63      56 55  52 51  48 47      40 39      16 15       0
[base 31:24][flags][lim 19:16][access][base 23:0][limit 15:0]
```

| Selector | Descriptor | Access | Flags | Meaning |
|----------|------------|--------|-------|---------|
| `0x00` | null | | | required, never used |
| `0x08` | 32-bit code | `0x9A` | `0xC` | present, ring 0, code, readable; 4 KiB pages, 32-bit |
| `0x10` | data | `0x92` | `0xC` | present, ring 0, data, writable |
| `0x18` | 64-bit code | `0x9A` | `0xA` | same, but L=1 (long mode), DB=0 |

Base 0 and limit `0xFFFFF` pages = 4 GiB: a "flat" model where segment
offsets equal linear addresses. The 64-bit descriptor is unused until
stage 5 but lives in the same table.

## The switch

```nasm
cli                         ; the real-mode IVT is about to become garbage
lgdt [gdt_descriptor]
mov  eax, cr0
or   eax, 1                 ; CR0.PE
mov  cr0, eax
jmp  dword 0x08:pm_entry    ; far jump: loads CS from the GDT
```

Setting `CR0.PE` does not change `CS`; the CPU keeps executing 16-bit code
until a far jump loads a 32-bit code selector. The jump also flushes
instructions already decoded in real mode. `pm_entry` then loads `0x10` into
every data segment register and sets up a 32-bit stack.

Interrupts stay off from here to the kernel: there is no IDT yet, and the
BIOS handlers are real-mode code.

## Printing without the BIOS

`INT 10h` is gone, so `print32` writes straight into VGA text memory at
`0xB8000`: 80×25 cells of two bytes (character, attribute `0x0A` = light
green). Before switching, stage2 asks the BIOS for the cursor row (`INT 10h,
AH=03h`) so protected-mode output continues below the real-mode log instead
of overwriting it. When the row passes 24 the screen scrolls with `rep movsd`.

The character is kept in `CL`: an earlier version kept it in `AH`, which the
`movzx eax, byte [vga_row]` offset computation overwrote, so every
protected-mode line came out as garbage on screen while serial looked fine.
`make test` now reads back the VGA buffer to catch exactly that.
