; =============================================================================
; fat12.inc — find and load a file from the FAT12 root directory (real mode)
; =============================================================================
; Geometry comes from the BPB, which stage1 left in memory at 0x7C00:
;
;   root_lba  = reserved + num_fats * sectors_per_fat
;   root_secs = root_entries * 32 / 512
;   data_lba  = root_lba + root_secs
;   cluster N lives at data_lba + (N - 2) * sectors_per_cluster
;
; FAT12 packs two 12-bit entries into three bytes. For cluster N, read the
; 16-bit word at byte offset N + N/2: an even N takes the low 12 bits, an odd
; N the high 12 bits. Values >= 0xFF8 end the chain.
;
; Scratch buffers (both below the EBDA, out of the way of stage2):
;   FAT_ROOT_SEG:0  root directory (up to 14 sectors = 7 KiB)
;   FAT_TABLE_SEG:0 first FAT      (up to 9 sectors  = 4.5 KiB)
; =============================================================================

FAT_ROOT_SEG  equ 0x6000        ; physical 0x60000
FAT_TABLE_SEG equ 0x6200        ; physical 0x62000

; BPB fields inside stage1's sector at 0x7C00
BPB_BASE         equ 0x7C00
bpb_sec_per_clus equ BPB_BASE + 13
bpb_reserved     equ BPB_BASE + 14
bpb_num_fats     equ BPB_BASE + 16
bpb_root_entries equ BPB_BASE + 17
bpb_sec_per_fat  equ BPB_BASE + 22
bpb_spt          equ BPB_BASE + 24
bpb_heads        equ BPB_BASE + 26

; -----------------------------------------------------------------------------
; fat12_init — compute root/data LBAs, read the root directory and the FAT
; -----------------------------------------------------------------------------
fat12_init:
    pusha
    push es

    ; root_lba = reserved + num_fats * sec_per_fat
    xor  ax, ax
    mov  al, [bpb_num_fats]
    mul  word [bpb_sec_per_fat]
    add  ax, [bpb_reserved]
    mov  [fat_root_lba], ax

    ; root_secs = (root_entries * 32 + 511) / 512
    mov  ax, [bpb_root_entries]
    shl  ax, 5
    add  ax, 511
    shr  ax, 9
    mov  [fat_root_secs], ax

    ; data_lba = root_lba + root_secs
    add  ax, [fat_root_lba]
    mov  [fat_data_lba], ax

    ; Read the root directory
    mov  ax, FAT_ROOT_SEG
    mov  es, ax
    xor  bx, bx
    mov  ax, [fat_root_lba]
    mov  cx, [fat_root_secs]
.root:
    call read_sector
    add  bx, 512
    inc  ax
    loop .root

    ; Read the first FAT
    mov  ax, FAT_TABLE_SEG
    mov  es, ax
    xor  bx, bx
    mov  ax, [bpb_reserved]
    mov  cx, [bpb_sec_per_fat]
.fat:
    call read_sector
    add  bx, 512
    inc  ax
    loop .fat

    pop  es
    popa
    ret

; -----------------------------------------------------------------------------
; fat12_load_file — load a root-directory file to DX:0000
;   DS:SI = 11-byte 8.3 name, space padded ("KERNEL  ELF")
;   DX    = destination segment (file may be larger than 64 KiB)
;   [fat_max_size] = largest file the caller has room for
; Returns CF=0 and EAX = file size on success.
; Returns CF=1 with EAX = 0 if the file is missing, or EAX = its size if it is
; larger than [fat_max_size] (nothing is loaded in that case).
; -----------------------------------------------------------------------------
fat12_load_file:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push fs

    ; --- Search the root directory -------------------------------------------
    mov  ax, FAT_ROOT_SEG
    mov  es, ax
    xor  di, di
    mov  cx, [bpb_root_entries]
.search:
    cmp  byte [es:di], 0        ; 0x00: no more entries
    je   .not_found
    test byte [es:di + 11], 0x18 ; skip volume labels and directories
    jnz  .next
    push cx
    push si
    push di
    mov  cx, 11
    repe cmpsb
    pop  di
    pop  si
    pop  cx
    je   .found
.next:
    add  di, 32
    loop .search
.not_found:
    xor  eax, eax
    stc
    jmp  .out

.found:
    mov  eax, [es:di + 28]      ; file size
    cmp  eax, [fat_max_size]
    ja   .too_big
    mov  [fat_file_size], eax
    mov  ax, [es:di + 26]       ; first cluster

    ; --- Follow the cluster chain --------------------------------------------
    mov  es, dx                 ; destination segment
    mov  dx, FAT_TABLE_SEG
    mov  fs, dx
.cluster:
    cmp  ax, 0xFF8
    jae  .done
    cmp  ax, 2
    jb   .done                  ; free/reserved: corrupt chain, stop
    push ax

    ; LBA = data_lba + (cluster - 2) * sec_per_clus
    sub  ax, 2
    xor  cx, cx
    mov  cl, [bpb_sec_per_clus]
    mul  cx
    add  ax, [fat_data_lba]
.sector:
    xor  bx, bx
    call read_sector
    mov  bx, es
    add  bx, 0x20               ; advance 512 bytes by bumping the segment
    mov  es, bx
    inc  ax
    loop .sector

    ; next = FAT[cluster + cluster/2], 12 bits
    pop  ax
    mov  bx, ax
    shr  bx, 1
    add  bx, ax
    mov  dx, [fs:bx]
    test ax, 1
    jz   .even
    shr  dx, 4
    jmp  .have_next
.even:
    and  dx, 0x0FFF
.have_next:
    mov  ax, dx
    jmp  .cluster

.done:
    mov  eax, [fat_file_size]
    clc
    jmp  .out
.too_big:
    stc
.out:
    pop  fs
    pop  es
    pop  di
    pop  si
    pop  dx
    pop  cx
    pop  bx
    ret
