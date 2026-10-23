# Stage 5 — Long mode

Source: [`boot/paging.inc`](../boot/paging.inc), [`boot/print64.inc`](../boot/print64.inc)

## Is it a 64-bit CPU?

`check_long_mode` first proves `CPUID` exists (bit 21 of `EFLAGS`, the ID
flag, can be toggled), then checks that extended leaf `0x80000001` exists
and that its `EDX` bit 29 (LM) is set. Without it, stage2 prints a fatal
message instead of triple-faulting.

## Page tables

Long mode cannot run without paging. Four levels translate a 48-bit
virtual address: PML4 → PDPT → PD → PT. With the PS bit set in a PD entry,
the walk stops there and the entry maps a 2 MiB page, so three tables are
enough to identity-map the first 1 GiB:

```
PML4 @ 0x1000   [0] = 0x2003      → PDPT (present, writable)
PDPT @ 0x2000   [0] = 0x3003      → PD
PD   @ 0x3000   [i] = i*2MiB | 0x83   (present, writable, PS)   i = 0..511
```

Identity mapping (virtual = physical) means the code currently running keeps
working the moment paging turns on, and the kernel can use physical
addresses directly. The kernel keeps running on these same tables.

## The switch

```nasm
mov eax, cr4
or  eax, 1 << 5         ; CR4.PAE: 64-bit page table entries
mov cr4, eax
mov eax, 0x1000
mov cr3, eax            ; PML4
mov ecx, 0xC0000080     ; IA32_EFER
rdmsr
or  eax, 1 << 8         ; EFER.LME: long mode enable
wrmsr
mov eax, cr0
or  eax, 1 << 31        ; CR0.PG: paging on → long mode active
mov cr0, eax
jmp 0x18:lm_entry       ; 64-bit code segment
```

After `CR0.PG` the CPU is in long mode but still in *compatibility* mode,
running 32-bit code. The far jump to the descriptor with `L=1` enters real
64-bit mode. `lm_entry` reloads the data segments (their bases are ignored
now, but they must hold valid selectors) and sets the stack to `0x80000`.

## 64-bit output

`print64.inc` is the same VGA + COM1 printer as `print32.inc`. It exists
twice because `pushad` and `popad` do not exist in 64-bit mode. Both share
`vga_row` and `vga_col`, which end up in `boot_info` for the kernel.
