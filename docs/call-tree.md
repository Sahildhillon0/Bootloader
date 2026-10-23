# Function call graphs and the function tree

How every function in the project is reached, from the BIOS jump to the
shell. C edges come from `gcc -fcallgraph-info`. Assembly edges and calls
through function pointers (dashed lines) come from the source.

- [1. The whole boot chain](#1-the-whole-boot-chain)
- [2. Stage1](#2-stage1)
- [3. Stage2](#3-stage2)
- [4. Kernel initialisation](#4-kernel-initialisation)
- [5. Interrupt path](#5-interrupt-path)
- [6. Shell](#6-shell)
- [7. Output funnel](#7-output-funnel)
- [8. Tree of all functions, by file](#8-tree-of-all-functions-by-file)
- [9. Full call tree (text)](#9-full-call-tree-text)

Legend for the flowcharts: solid arrow = direct call, thick arrow = jump that
never returns, dashed arrow = call through a function pointer.

---

## 1. The whole boot chain

```mermaid
flowchart LR
    BIOS(["BIOS<br/>loads LBA 0 to 0x7C00"]) ==> s1["stage1<br/>_start / _init<br/>16-bit"]
    s1 ==> s2["stage2_start<br/>0x7E00, 16-bit"]
    s2 ==> pm["pm_entry<br/>32-bit"]
    pm ==> elm["enter_long_mode"]
    elm ==> lm["lm_entry<br/>64-bit"]
    lm --> el["elf_load (C)"]
    lm ==> ks["kernel _start<br/>0x100000"]
    ks --> km["kmain"]
    km --> sh["shell_run<br/>never returns"]
```

## 2. Stage1

```mermaid
flowchart TD
    start["_start"] ==> init["_init"]
    init --> ps["print_string"]
    init --> de["detect_ext"]
    init --> rs["read_sector<br/>x63"]
    rs -. "5 failures" .-> derr["disk_error"]
    derr --> ps
    derr ==> halt["halt"]
    init ==> s2(["jmp 0x0000:0x7E00"])
```

## 3. Stage2

```mermaid
flowchart TD
    subgraph RM["16-bit real mode"]
        s2["stage2_start"]
        s2 --> si["serial_init"]
        s2 --> p16["print16"] --> pc16["putc16"]
        s2 --> hx["print_hex16"] --> pc16
        s2 --> ea["enable_a20"] --> ca["check_a20"]
        s2 --> e820["read_e820"]
        s2 --> de["detect_ext"]
        s2 --> fi["fat12_init"] --> rs["read_sector"]
        s2 --> fl["fat12_load_file"] --> rs
        rs -. "5 failures" .-> derr["disk_error"] ==> f16["fatal16"]
        s2 -. "any failure" .-> f16
        f16 --> p16
        f16 ==> h16["halt16"]
    end
    subgraph PM["32-bit protected mode"]
        pm["pm_entry"]
        pm --> p32["print32"] --> pc32["putc32"]
        pm --> clm["check_long_mode"]
        pm --> sp["setup_paging"]
        pm ==> elm["enter_long_mode"]
    end
    subgraph LM["64-bit long mode"]
        lm["lm_entry"]
        lm --> p64["print64"] --> pc64["putc64"]
        lm --> el["elf_load"]
        el --> cb["copy_bytes"]
        el --> zb["zero_bytes"]
    end
    s2 ==> pm
    elm ==> lm
    lm ==> k(["kernel _start<br/>RDI = &boot_info"])
```

## 4. Kernel initialisation

```mermaid
flowchart TD
    st["_start"] --> km["kmain"]
    km --> si["serial_init"]
    km --> vi["vga_init"] --> uc["update_cursor"]
    km --> vsc["vga_set_color"]
    km --> pmm["print_memory_map"] --> et["e820_type"]
    km --> gi["gdt_init"]
    km --> ii["idt_init"] --> sg["set_gate"]
    km --> pi["pic_init"] --> iow["io_wait"]
    km --> ti["timer_init"] --> irq["irq_install"] --> pu["pic_unmask"]
    km --> ts["timer_sleep"]
    km --> tt["timer_ticks"]
    km --> ki["keyboard_init"] --> irq
    km --> ai["ata_init"]
    ai --> d4["delay400"]
    ai --> wnb["wait_not_busy"]
    ai --> wd["wait_drq"]
    ai --> as["ata_string"]
    km --> fm["fat12_mount"] --> ar["ata_read"]
    ar --> d4
    ar --> wnb
    ar --> wd
    km --> kp["kprintf"]
    km ==> sr["shell_run"]
```

## 5. Interrupt path

Every interrupt enters through a generated stub; `isr_dispatch` decides what
to do with it.

```mermaid
flowchart TD
    cpu(["CPU exception<br/>or PIC IRQ"]) ==> stub["isr_0 ... isr_47"]
    stub ==> com["isr_common<br/>saves registers"]
    com --> dis["isr_dispatch"]
    dis -->|"vector >= 32"| spur["pic_is_spurious"]
    dis -.->|"IRQ 0"| tirq["timer_irq<br/>ticks++"]
    dis -.->|"IRQ 1"| kirq["keyboard_irq"] --> push["push<br/>ring buffer"]
    dis -->|"IRQ done"| eoi["pic_eoi"]
    dis -->|"vector 3"| bp["kprintf<br/>resume"]
    dis -->|"other exception"| dump["dump_regs"]
    dis --> cr2["read_cr2<br/>page fault"]
    dis ==> hf["halt_forever"]
```

## 6. Shell

```mermaid
flowchart TD
    sr["shell_run"] --> rl["read_line"]
    rl --> ig["input_getc"]
    ig --> sg["serial_getc"]
    ig -.-> kb(["keyboard ring buffer<br/>filled by keyboard_irq"])
    rl --> vc["vga_clear<br/>Ctrl-L"]
    sr --> spl["split"]
    sr --> sc["strcmp"]
    sr -.-> help["cmd_help"]
    sr -.-> clr["cmd_clear"] --> vc
    sr -.-> echo["cmd_echo"]
    sr -.-> mem["cmd_mem"]
    sr -.-> ls["cmd_ls"]
    ls --> ai["ata_info"]
    ls --> fl["fat12_list"]
    fl --> ent["entry"]
    fl --> isf["is_file"]
    fl --> nf["name_from_83"]
    fl -.-> pd["print_dirent"]
    sr -.-> cat["cmd_cat"] --> fr["fat12_read"]
    fr --> nt["name_to_83"] --> ms["memset"]
    fr --> ent
    fr --> isf
    fr --> mc["memcmp"]
    fr --> fn["fat_next"]
    fr --> ar["ata_read"]
    fr --> cpy["memcpy"]
    sr -.-> disk["cmd_disk"] --> ai
    sr -.-> up["cmd_uptime"] --> tt["timer_ticks"]
    sr -.-> crash["cmd_crash<br/>page fault"]
    sr -.-> rb["cmd_reboot"] --> hf["halt_forever"]
    sr -.-> sd["cmd_shutdown"] --> hf
```

## 7. Output funnel

All kernel output ends in `console_putc`, which feeds both COM1 and the
screen.

```mermaid
flowchart TD
    kp["kprintf"] --> kv["kvprintf"]
    kv --> pn["put_number"]
    kv --> pad["pad"]
    pn --> pad
    kv --> cps["console_puts"]
    pn --> cp["console_putc"]
    pad --> cp
    kv --> cp
    cps --> cp
    cp --> sp["serial_putc<br/>COM1 0x3F8"]
    cp --> vp["vga_putc<br/>0xB8000"]
    vp --> scr["scroll"]
    vp --> uc["update_cursor<br/>CRTC 0x3D4"]
```

---

## 8. Tree of all functions, by file

Every function and routine in the project, grouped by directory and file.

```mermaid
mindmap
  root((Bootloader))
    boot
      stage1.asm
        _start
        _init
        disk_error
        halt
        print_string
      disk.inc
        detect_ext
        read_sector
      stage2.asm
        stage2_start
        disk_error
        fatal16
        halt16
        pm_entry
        lm_entry
      print16.inc
        serial_init
        putc16
        print16
        print_hex16
      a20.inc
        check_a20
        enable_a20
      e820.inc
        read_e820
      fat12.inc
        fat12_init
        fat12_load_file
      print32.inc
        putc32
        print32
      paging.inc
        check_long_mode
        setup_paging
        enter_long_mode
      print64.inc
        putc64
        print64
      elf_loader.c
        copy_bytes
        zero_bytes
        elf_load
    kernel
      entry.asm
        _start
      kmain.c
        e820_type
        print_memory_map
        kmain
      io.h
        outb outw inb inw
        io_wait
        cli sti hlt
        read_cr2
        halt_forever
      string.c
        memset memcpy memmove memcmp
        strlen strcmp strncmp
      serial.c
        serial_init
        serial_putc
        serial_getc
      vga.c
        update_cursor
        scroll
        vga_init
        vga_clear
        vga_set_color
        vga_putc
      console.c
        console_putc
        console_puts
        pad
        put_number
        kvprintf
        kprintf
      gdt.c
        gdt_init
      isr.asm
        isr_0 to isr_47
        isr_common
      idt.c
        set_gate
        idt_init
        irq_install
        dump_regs
        isr_dispatch
      pic.c
        pic_init
        pic_unmask
        pic_mask
        pic_eoi
        pic_is_spurious
      timer.c
        timer_irq
        timer_init
        timer_ticks
        timer_sleep
      keyboard.c
        push
        keyboard_irq
        keyboard_init
        input_getc
      ata.c
        delay400
        wait_not_busy
        wait_drq
        ata_string
        ata_init
        ata_info
        ata_read
      fat12.c
        fat12_mount
        fat_next
        name_from_83
        name_to_83
        entry
        is_file
        fat12_list
        fat12_read
      shell.c
        cmd_help cmd_clear cmd_echo
        cmd_mem cmd_ls cmd_cat
        cmd_disk cmd_uptime cmd_crash
        cmd_reboot cmd_shutdown
        print_dirent
        read_line
        split
        shell_run
```

---

## 9. Full call tree (text)

The same graph as one indented tree, in execution order. `⇒` is a jump that
does not return, `⇢` a call through a function pointer, and `…` means
the subtree was already expanded above. Port I/O helpers from `io.h`
(`inb`, `outb`, …) are left out to keep it readable.

```
BIOS ⇒ stage1 _start                                    boot/stage1.asm
└─ _init
   ├─ print_string
   ├─ detect_ext                                        boot/disk.inc
   ├─ read_sector  (×63: LBA 1..63 → 0x7E00)
   │  └─ disk_error ⇒ halt              (after 5 failed attempts)
   └─ ⇒ stage2_start                                    boot/stage2.asm
      ├─ serial_init                                    boot/print16.inc
      ├─ print16 ─ putc16
      ├─ print_hex16 ─ putc16
      ├─ enable_a20 ─ check_a20                         boot/a20.inc
      ├─ read_e820                                      boot/e820.inc
      ├─ detect_ext
      ├─ fat12_init ─ read_sector                       boot/fat12.inc
      ├─ fat12_load_file ─ read_sector
      ├─ fatal16 ─ print16, ⇒ halt16    (on any failure)
      └─ ⇒ pm_entry                     (CR0.PE, far jump 0x08)
         ├─ print32 ─ putc32                            boot/print32.inc
         ├─ check_long_mode                             boot/paging.inc
         ├─ setup_paging
         └─ ⇒ enter_long_mode           (PAE, LME, PG, far jump 0x18)
            └─ ⇒ lm_entry
               ├─ print64 ─ putc64                      boot/print64.inc
               ├─ elf_load                              boot/elf_loader.c
               │  ├─ copy_bytes
               │  └─ zero_bytes
               └─ ⇒ kernel _start       (RDI = &boot_info)  kernel/entry.asm
                  └─ kmain                              kernel/kmain.c
                     ├─ serial_init                     kernel/serial.c
                     ├─ vga_init ─ update_cursor        kernel/vga.c
                     ├─ vga_set_color
                     ├─ kprintf ─ kvprintf              kernel/console.c
                     │  ├─ put_number ─ pad, console_putc
                     │  ├─ pad ─ console_putc
                     │  ├─ console_puts ─ console_putc
                     │  └─ console_putc
                     │     ├─ serial_putc
                     │     └─ vga_putc ─ scroll, update_cursor
                     ├─ print_memory_map ─ e820_type, kprintf …
                     ├─ gdt_init                        kernel/gdt.c
                     ├─ idt_init ─ set_gate             kernel/idt.c
                     ├─ (int3 → interrupt path, see below)
                     ├─ pic_init ─ io_wait              kernel/pic.c
                     ├─ timer_init ─ irq_install ─ pic_unmask   kernel/timer.c
                     ├─ timer_sleep
                     ├─ timer_ticks
                     ├─ keyboard_init ─ irq_install …   kernel/keyboard.c
                     ├─ ata_init                        kernel/ata.c
                     │  ├─ delay400
                     │  ├─ wait_not_busy
                     │  ├─ wait_drq
                     │  └─ ata_string
                     ├─ ata_info
                     ├─ fat12_mount ─ ata_read          kernel/fat12.c
                     │                ├─ delay400, wait_not_busy, wait_drq
                     └─ ⇒ shell_run                     kernel/shell.c
                        ├─ console_puts …, vga_set_color, kprintf …
                        ├─ read_line
                        │  ├─ input_getc ─ serial_getc  (+ keyboard ring buffer)
                        │  ├─ console_putc …, console_puts …
                        │  └─ vga_clear ─ update_cursor
                        ├─ split
                        ├─ strcmp                       kernel/string.c
                        └─ ⇢ commands[].fn
                           ├─ cmd_help ─ kprintf …
                           ├─ cmd_clear ─ vga_clear …
                           ├─ cmd_echo ─ kprintf …
                           ├─ cmd_mem ─ kprintf …
                           ├─ cmd_ls ─ ata_info, kprintf …
                           │  └─ fat12_list ─ entry, is_file, name_from_83
                           │     └─ ⇢ print_dirent ─ kprintf …
                           ├─ cmd_cat ─ console_putc …, kprintf …
                           │  └─ fat12_read
                           │     ├─ name_to_83 ─ memset
                           │     ├─ entry, is_file, memcmp, memcpy
                           │     ├─ fat_next
                           │     └─ ata_read …
                           ├─ cmd_disk ─ ata_info, kprintf …
                           ├─ cmd_uptime ─ timer_ticks, kprintf …
                           ├─ cmd_crash ─ kprintf …  (read 0x40000000 → #PF)
                           ├─ cmd_reboot ─ kprintf …, ⇒ halt_forever
                           └─ cmd_shutdown ─ kprintf …, ⇒ halt_forever

Interrupt path (any time interrupts are on)             kernel/isr.asm
CPU ⇒ isr_N ⇒ isr_common
              └─ isr_dispatch                           kernel/idt.c
                 ├─ pic_is_spurious                     (IRQ 7 / 15)
                 ├─ ⇢ irq_handlers[irq]
                 │  ├─ timer_irq       (IRQ 0)          kernel/timer.c
                 │  └─ keyboard_irq ─ push   (IRQ 1)    kernel/keyboard.c
                 ├─ pic_eoi
                 ├─ kprintf …          (vector 3: breakpoint, resumes)
                 └─ vga_set_color, kprintf …, read_cr2, dump_regs ─ kprintf …,
                    ⇒ halt_forever     (any other exception)

Present but not called today: memmove, strlen, strncmp, pic_mask.
```
