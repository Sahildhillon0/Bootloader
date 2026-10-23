# Memory graph

Where everything lives in physical memory, who writes and reads each region,
and how the layout changes as the machine moves from the BIOS to the
kernel shell.

- [1. Physical memory map](#1-physical-memory-map)
- [2. Which function touches which memory](#2-which-function-touches-which-memory)
- [3. Memory over the boot timeline](#3-memory-over-the-boot-timeline)
- [4. Disk to memory](#4-disk-to-memory)
- [5. Page tables](#5-page-tables)
- [6. Kernel image at 1 MiB](#6-kernel-image-at-1-mib)
- [7. boot_info](#7-boot_info)
- [8. Interrupt stack frame](#8-interrupt-stack-frame)

All addresses are physical. Paging is an identity map, so virtual = physical
everywhere.

---

## 1. Physical memory map

Lowest address at the top.

```mermaid
flowchart TB
    subgraph LOW["Conventional memory, below 1 MiB"]
        direction TB
        ivt["0x00000 - 0x004FF<br/>IVT + BIOS Data Area<br/>(BIOS)"]
        free1["0x00500 - 0x00FFF<br/>free"]
        pt["0x01000 - 0x03FFF<br/>Page tables: PML4, PDPT, PD<br/>(setup_paging, used by the kernel forever)"]
        free2["0x04000 - 0x04FFF<br/>free"]
        e820["0x05000 - 0x055FF<br/>E820 map, up to 64 x 24 B<br/>(read_e820)"]
        stk16["0x05600 - 0x07BFF<br/>real/protected mode stack, grows down from 0x7C00"]
        s1["0x07C00 - 0x07DFF<br/>stage1 + BPB<br/>(BIOS loads it)"]
        s2["0x07E00 - 0x0FDFF<br/>stage2, up to 63 sectors<br/>(stage1 loads it)"]
        kbuf["0x10000 - 0x5FFFF<br/>KERNEL.ELF file buffer, max 320 KiB<br/>(fat12_load_file)"]
        fatb["0x60000 - 0x63FFF<br/>root directory 0x60000, FAT 0x62000<br/>(fat12_init)"]
        stk64["0x64000 - 0x7FFFF<br/>long mode stack, top 0x80000<br/>(lm_entry, elf_load)"]
        ebda["0x80000 - 0x9FFFF<br/>EBDA starts at 0x9FC00 in QEMU (reserved)"]
        vga["0xA0000 - 0xBFFFF<br/>video memory, text buffer at 0xB8000"]
        rom["0xC0000 - 0xFFFFF<br/>option ROMs, BIOS"]
        ivt --- free1 --- pt --- free2 --- e820 --- stk16 --- s1 --- s2 --- kbuf --- fatb --- stk64 --- ebda --- vga --- rom
    end
    subgraph HIGH["Above 1 MiB"]
        direction TB
        ktext["0x100000 kernel .text"]
        krod["0x103000 .rodata"]
        kdata["0x105000 .data + .bss<br/>stack 16 KiB, FAT cache, file_buf 64 KiB"]
        kend["about 0x1209D2 __kernel_end"]
        freeh["free RAM up to 0x7FDFFFF (128 MiB guest)"]
        idmax["0x3FFFFFFF end of the 1 GiB identity map<br/>cmd_crash reads 0x40000000"]
        ktext --- krod --- kdata --- kend --- freeh --- idmax
    end
    LOW --- HIGH
```

| Region | Size | Written by | Read by | Lifetime |
|--------|------|------------|---------|----------|
| `0x01000–0x03FFF` page tables | 12 KiB | `setup_paging` | CPU (CR3) | forever |
| `0x05000` E820 map | ≤ 1.5 KiB | `read_e820` | `print_memory_map`, `cmd_mem` | forever |
| `0x07C00` stage1 + BPB | 512 B | BIOS | `fat12.inc`, `disk.inc` (`bpb_*`) | until the kernel |
| `0x07E00` stage2 | ≤ 31.5 KiB | stage1 `_init` | CPU | until the kernel |
| `0x10000` kernel file | ≤ 320 KiB | `fat12_load_file` | `elf_load` | until the kernel |
| `0x60000` FAT12 buffers | 11.5 KiB | `fat12_init` | `fat12_load_file` | stage2 only |
| `0x80000` stack top | — | `lm_entry` | `elf_load` | stage2 only |
| `0xB8000` VGA text | 4000 B | `putc32`, `putc64`, `vga_putc` | the display | forever |
| `0x100000+` kernel | ≈ 130 KiB | `elf_load` | the kernel | forever |

---

## 2. Which function touches which memory

Functions on the left, memory regions on the right. `W` = writes,
`R` = reads, `RW` = both.

```mermaid
flowchart LR
    subgraph F16["stage1 / stage2, 16-bit"]
        bios(["BIOS"])
        init["stage1 _init"]
        rs["read_sector"]
        re["read_e820"]
        ca["check_a20"]
        fi["fat12_init"]
        fl["fat12_load_file"]
        pc16["putc16 (INT 10h)"]
    end
    subgraph F32["stage2, 32/64-bit"]
        sp["setup_paging"]
        pc32["putc32 / putc64"]
        el["elf_load"]
        lm["lm_entry"]
    end
    subgraph FK["kernel"]
        km["kmain / print_memory_map"]
        cm["cmd_mem"]
        vp["vga_putc / scroll / vga_clear"]
        fm["fat12_mount / fat12_read"]
        cc["cmd_crash"]
        cpu(["CPU page walk"])
    end

    M_IVT[("0x00500 / 0x100500<br/>A20 probe bytes")]
    M_PT[("0x1000-0x3FFF<br/>page tables")]
    M_E820[("0x5000<br/>E820 map")]
    M_S1[("0x7C00<br/>stage1 + BPB")]
    M_S2[("0x7E00<br/>stage2")]
    M_KF[("0x10000<br/>KERNEL.ELF bytes")]
    M_FAT[("0x60000<br/>root + FAT buffers")]
    M_VGA[("0xB8000<br/>VGA text")]
    M_BI[("boot_info<br/>in stage2 .data")]
    M_K[("0x100000+<br/>kernel segments")]
    M_KC[("kernel .bss<br/>fat[], root[], file_buf")]
    M_BAD[("0x40000000<br/>unmapped")]

    bios -->|W| M_S1
    init -->|W via read_sector| M_S2
    rs -->|W| M_S2
    rs -->|W| M_FAT
    rs -->|W| M_KF
    ca -->|RW then restore| M_IVT
    re -->|W| M_E820
    fi -->|R BPB| M_S1
    fi -->|W| M_FAT
    fl -->|R| M_FAT
    fl -->|W| M_KF
    pc16 -->|W| M_VGA
    sp -->|W| M_PT
    pc32 -->|RW| M_VGA
    el -->|R| M_KF
    el -->|W copy + zero .bss| M_K
    el -->|W kernel_start/end/entry| M_BI
    lm -->|W magic, drive, mmap, cursor| M_BI
    km -->|R| M_BI
    km -->|R| M_E820
    cm -->|R| M_E820
    vp -->|RW| M_VGA
    fm -->|W via ata_read| M_KC
    cpu -->|R| M_PT
    cc -->|R faults| M_BAD
```

---

## 3. Memory over the boot timeline

Each step adds or retires regions. Once the kernel runs, everything below
`0x10000` except the page tables, the E820 map and the BIOS areas is dead
space it may reuse.

```mermaid
flowchart TD
    t0["BIOS POST<br/>IVT, BDA, EBDA, ROMs exist"]
    t1["BIOS loads LBA 0<br/>+ 0x7C00 stage1"]
    t2["stage1 runs<br/>+ stack below 0x7C00<br/>+ 0x7E00 stage2 (63 sectors)"]
    t3["stage2 real mode<br/>+ A20 on: above 1 MiB reachable<br/>+ 0x5000 E820 map<br/>+ 0x60000 root dir + FAT<br/>+ 0x10000 KERNEL.ELF bytes"]
    t4["protected mode<br/>+ 0x1000-0x3FFF page tables<br/>VGA written directly"]
    t5["long mode<br/>stack moves to 0x80000"]
    t6["elf_load<br/>+ 0x100000 .text .rodata .data<br/>+ .bss zeroed (kernel stack inside)<br/>boot_info filled"]
    t7["kernel runs<br/>new GDT and IDT inside the kernel image<br/>stage2, file buffer, FAT buffers: no longer used<br/>page tables and E820 map: still used"]
    t0 --> t1 --> t2 --> t3 --> t4 --> t5 --> t6 --> t7
```

---

## 4. Disk to memory

What gets copied from which LBA to which address, and by whom.

```mermaid
flowchart LR
    subgraph DISK["boot.img, 2880 sectors"]
        d0["LBA 0<br/>stage1 + BPB"]
        d1["LBA 1-63<br/>stage2"]
        dfat["LBA 64-72<br/>FAT 1"]
        droot["LBA 82-95<br/>root directory"]
        dk["LBA 96+<br/>KERNEL.ELF clusters"]
        dh["HELLO.TXT clusters"]
    end
    subgraph RAM["memory"]
        m7c["0x7C00"]
        m7e["0x7E00"]
        m62["0x62000 FAT"]
        m60["0x60000 root"]
        m10["0x10000 file buffer"]
        m1m["0x100000 kernel"]
        kc["kernel fat[] / root[]"]
        fb["kernel file_buf"]
    end
    d0 -->|"BIOS"| m7c
    d1 -->|"stage1 read_sector"| m7e
    dfat -->|"fat12_init"| m62
    droot -->|"fat12_init"| m60
    dk -->|"fat12_load_file"| m10
    m10 -->|"elf_load PT_LOAD copy"| m1m
    dfat -->|"fat12_mount, ATA"| kc
    droot -->|"fat12_mount, ATA"| kc
    dh -->|"fat12_read, ATA (cat)"| fb
```

---

## 5. Page tables

Built by `setup_paging`, loaded into `CR3` by `enter_long_mode`, kept by the
kernel. Three tables map 1 GiB with 2 MiB pages.

```mermaid
flowchart LR
    cr3(["CR3 = 0x1000"]) --> pml4
    subgraph pml4["PML4 at 0x1000"]
        p0["[0] = 0x2003"]
        prest["[1..511] = 0"]
    end
    p0 --> pdpt
    subgraph pdpt["PDPT at 0x2000"]
        q0["[0] = 0x3003"]
        qrest["[1..511] = 0"]
    end
    q0 --> pd
    subgraph pd["PD at 0x3000, PS = 1"]
        r0["[0] = 0x00000083 → 0 - 2 MiB"]
        r1["[1] = 0x00200083 → 2 - 4 MiB"]
        rdots["..."]
        r511["[511] = 0x3FE00083 → 1022 - 1024 MiB"]
    end
```

A virtual address `V < 1 GiB` decodes as PML4 index 0, PDPT index 0, PD
index `V >> 21`, offset `V & 0x1FFFFF`, giving the physical address `V`.
Anything at or above `0x40000000` hits an empty PDPT entry and page-faults,
which is exactly what `cmd_crash` shows.

---

## 6. Kernel image at 1 MiB

From `readelf -lW build/kernel.elf`, as placed by `elf_load`:

```mermaid
flowchart TB
    a["0x100000 .text (R E)<br/>_start first, then all code<br/>file size 0x276F"]
    b["0x103000 .rodata (R)<br/>strings, keymaps, isr_stub_table<br/>file size 0x1040"]
    c["0x105000 .data (RW)<br/>1 byte in the file"]
    d[".bss (RW), zeroed by elf_load<br/>kernel stack 16 KiB<br/>idt[256], keyboard ring buffer<br/>fat[] 6 KiB, root[] 16 KiB<br/>file_buf 64 KiB"]
    e["__kernel_end, about 0x1209D2"]
    a --> b --> c --> d --> e
```

The third `PT_LOAD` has `p_filesz = 0x1` and `p_memsz = 0x1B9D2`: one byte of
`.data`, then about 110 KiB of `.bss` that `elf_load` zero-fills.

---

## 7. boot_info

Filled by `lm_entry` and `elf_load` inside stage2's `.data`; the kernel
receives its address in `RDI`.

```mermaid
flowchart LR
    rdi(["RDI"]) --> bi
    subgraph bi["struct boot_info, 48 bytes"]
        direction TB
        f0["+0 magic = 0xB007B007"]
        f4["+4 boot_drive, +5 vga_row, +6 vga_col, +7 pad"]
        f8["+8 mmap_count, +12 pad"]
        f16["+16 mmap_addr = 0x5000"]
        f24["+24 kernel_start"]
        f32["+32 kernel_end"]
        f40["+40 kernel_entry"]
        f0 --- f4 --- f8 --- f16 --- f24 --- f32 --- f40
    end
    f16 --> e820[("E820 array at 0x5000<br/>mmap_count x 24 bytes")]
    f40 --> start[("_start at 0x100000")]
```

---

## 8. Interrupt stack frame

What `isr_dispatch` sees through its `struct regs *` argument. Highest
address at the top: the CPU pushes first, the stub second, `isr_common` last.

```mermaid
flowchart TB
    ss["SS"]
    rsp["RSP (interrupted)"]
    rfl["RFLAGS"]
    cs["CS"]
    rip["RIP"]
    err["error code (CPU or dummy 0)"]
    vec["vector (isr_N)"]
    gp["RAX RBX RCX RDX RSI RDI RBP<br/>R8 R9 R10 R11 R12 R13 R14 R15<br/>(isr_common, R15 at the lowest address)"]
    ptr(["RDI = RSP = struct regs *"])
    ss --- rsp --- rfl --- cs --- rip --- err --- vec --- gp --- ptr
```

| Pushed by | Fields |
|-----------|--------|
| CPU | `ss`, `rsp`, `rflags`, `cs`, `rip`, and for some exceptions `error` |
| `isr_N` stub | dummy `error` (if needed), `vector` |
| `isr_common` | `rax` … `r15` |
