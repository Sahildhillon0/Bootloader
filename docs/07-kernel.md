# Stage 7 — The kernel core

Source: [`kernel/`](../kernel)

## Compiling freestanding C with the host gcc

No cross-compiler is needed: the host's x86_64 gcc can emit kernel code with
the right flags:

| Flag | Why |
|------|-----|
| `-ffreestanding -nostdlib -fno-builtin` | no libc, no assumptions about it |
| `-mno-red-zone` | the SysV red zone (128 bytes below `RSP`) would be overwritten by interrupt frames |
| `-mgeneral-regs-only` | nobody saves SSE/x87 state on interrupts |
| `-fno-pic -fno-pie -mcmodel=small` | absolute code at 1 MiB |
| `-fno-stack-protector -fno-asynchronous-unwind-tables` | no runtime support for either |
| `-fno-tree-loop-distribute-patterns` | stop gcc from turning `memset`'s own loop into a call to `memset` |

gcc may still emit calls to `memcpy`/`memset`/`memmove`/`memcmp`, so
`string.c` provides them.

## Console

`console_putc` sends every byte to COM1 (with `\n` → `\r\n`) and to the VGA
driver. `vga.c` starts at the row stage2 handed over, so the whole boot log
stays on screen, and moves the hardware cursor through CRT controller
registers `0x0E`/`0x0F`. `kprintf` handles `%c %s %d %u %x %X %p %%`, the `l`
length modifier, widths and the `0`/`-` flags.

## GDT and IDT

The bootloader's GDT lives in stage2's memory, so `gdt_init` loads a kernel
copy (null, 64-bit code `0x08`, data `0x10`). `CS` can only change through a
far transfer, so it pushes the selector and a return address and runs
`lretq`.

The IDT has 16-byte gates; type `0x8E` is a present ring-0 interrupt gate,
which clears `IF` on entry. `isr.asm` generates 48 stubs: exceptions 0–31
and IRQs 0–15. Exceptions 8, 10–14, 17, 21, 29 and 30 push an error code;
the others push a dummy 0, so every frame looks the same:

```
r15 … rax (pushed by isr_common) | vector | error | rip cs rflags rsp ss (CPU)
```

`isr_dispatch` receives that frame as `struct regs *`:

- vector ≥ 32: an IRQ, sent to its handler, then EOI
- vector 3 (`int3`): print and return. `kmain` runs `int3` once at boot to
  prove the IDT round trip works
- anything else: red screen with the exception name, `CR2` for page faults,
  the registers, then halt

## PIC and PIT

The 8259 PICs deliver IRQ 0–7 on vectors 8–15 by default, which collide
with CPU exceptions. `pic_init` sends the ICW1–ICW4 sequence to move them
to 32–47, then masks everything except the cascade line. `irq_install`
unmasks a line when a handler is registered. Spurious IRQ 7/15 are detected
through the in-service register and not acknowledged.

The PIT's channel 0 counts down from `1193182 / 100` and raises IRQ 0 at
100 Hz. The handler only increments `ticks`; `timer_sleep` waits with `hlt`
in between.
