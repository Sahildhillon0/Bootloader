# =============================================================================
# Makefile — x86_64 Bootloader Project
# =============================================================================
# Toolchain requirements:
#   nasm     >= 2.15   (assembler)
#   gcc, ld            (host x86_64 toolchain, used freestanding)
#   qemu     >= 5.0    (qemu-system-x86_64)
#   mtools             (mcopy/mdir: write files into the FAT12 image)
#   xxd                (hex dumps)
#
# Install:
#   Arch:          sudo pacman -S nasm qemu-system-x86 qemu-ui-gtk mtools tinyxxd
#   Ubuntu/Debian: sudo apt install nasm gcc qemu-system-x86 mtools xxd
# =============================================================================

# --- Toolchain ----------------------------------------------------------------
NASM    := nasm
CC      := gcc
LD      := ld
QEMU    := qemu-system-x86_64
DD      := dd

# --- Directories --------------------------------------------------------------
BOOT_DIR    := boot
KERNEL_DIR  := kernel
INCLUDE_DIR := include
ROOTFS_DIR  := rootfs
BUILD_DIR   := build
SCRIPTS_DIR := scripts

# --- Output files -------------------------------------------------------------
DISK_IMG    := $(BUILD_DIR)/boot.img
STAGE1_BIN  := $(BUILD_DIR)/stage1.bin
STAGE2_BIN  := $(BUILD_DIR)/stage2.bin

# --- Disk geometry (must match the BPB in boot/stage1.asm) --------------------
# 1.44 MB = 2880 sectors; 64 reserved = stage1 + 63 sectors of stage2
DISK_SECTORS     := 2880
RESERVED_SECTORS := 64
FAT_SECTORS      := 9

# --- Flags --------------------------------------------------------------------
NASM_FLAGS := -i $(BOOT_DIR)/ -w+all

# Freestanding 64-bit C: no libc, no red zone (interrupts would trash it),
# no SSE (nobody saves the FPU state), no PIC.
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra \
          -m64 -ffreestanding -fno-builtin -nostdlib \
          -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
          -mno-red-zone -mgeneral-regs-only -mcmodel=small \
          -fno-tree-loop-distribute-patterns \
          -I$(INCLUDE_DIR)

# mtools refuses some hand-made BPBs without this
export MTOOLS_SKIP_CHECK := 1

# --- QEMU flags ---------------------------------------------------------------
# The image is attached as the primary IDE master: BIOS boots it as drive 0x80
# and the kernel's ATA driver can read it back.
QEMU_DRIVE  := -drive file=$(DISK_IMG),format=raw,if=ide,index=0,media=disk
QEMU_COMMON := -m 128M -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04

QEMU_FLAGS  := $(QEMU_DRIVE) $(QEMU_COMMON) \
               -serial stdio \
               -d int,cpu_reset,guest_errors -D $(BUILD_DIR)/qemu.log

QEMU_DEBUG_FLAGS := $(QEMU_FLAGS) -S -gdb tcp::1234

# =============================================================================
# Default target
# =============================================================================
.PHONY: all
all: $(DISK_IMG)
	@echo ""
	@echo "Build complete: $(DISK_IMG)"
	@echo "Run with: make run   (headless: make run-headless, tests: make test)"
	@echo ""

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# =============================================================================
# Stage 1: boot sector (flat 512-byte binary)
# =============================================================================
$(STAGE1_BIN): $(BOOT_DIR)/stage1.asm $(BOOT_DIR)/disk.inc | $(BUILD_DIR)
	@echo "[NASM] stage1"
	$(NASM) $(NASM_FLAGS) -f bin -o $@ $<
	@size=$$(stat -c%s $@); \
	 if [ $$size -ne 512 ]; then \
	     echo "ERROR: stage1.bin is $$size bytes (expected 512)"; exit 1; \
	 fi

# =============================================================================
# Stage 2: real mode → protected mode → long mode loader (asm + C)
# =============================================================================
STAGE2_ASM_INC := $(wildcard $(BOOT_DIR)/*.inc)
STAGE2_OBJS    := $(BUILD_DIR)/stage2.o $(BUILD_DIR)/elf_loader.o

$(BUILD_DIR)/stage2.o: $(BOOT_DIR)/stage2.asm $(STAGE2_ASM_INC) | $(BUILD_DIR)
	@echo "[NASM] stage2"
	@# Absolute relocations are intended: stage2.ld keeps stage2 below 64 KiB
	$(NASM) $(NASM_FLAGS) -w-reloc -f elf64 -o $@ $<

$(BUILD_DIR)/elf_loader.o: $(BOOT_DIR)/elf_loader.c $(wildcard $(INCLUDE_DIR)/*.h) | $(BUILD_DIR)
	@echo "[CC]   elf_loader"
	$(CC) $(CFLAGS) -c -o $@ $<

$(STAGE2_BIN): $(STAGE2_OBJS) $(BOOT_DIR)/stage2.ld
	@echo "[LD]   stage2"
	$(LD) -nostdlib -T $(BOOT_DIR)/stage2.ld -o $@ $(STAGE2_OBJS)
	@echo "[OK]   stage2.bin = $$(stat -c%s $@) bytes (max $$((63 * 512)))"

# =============================================================================
# Kernel: static ELF64 at 1 MiB
# =============================================================================
KERNEL_ELF   := $(BUILD_DIR)/kernel.elf
KERNEL_C     := $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_ASM   := $(wildcard $(KERNEL_DIR)/*.asm)
KERNEL_OBJS  := $(patsubst $(KERNEL_DIR)/%.asm,$(BUILD_DIR)/kernel/%.asm.o,$(KERNEL_ASM)) \
                $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_C))
KERNEL_HDRS  := $(wildcard $(KERNEL_DIR)/*.h) $(wildcard $(INCLUDE_DIR)/*.h)

$(BUILD_DIR)/kernel:
	mkdir -p $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c $(KERNEL_HDRS) | $(BUILD_DIR)/kernel
	@echo "[CC]   $<"
	$(CC) $(CFLAGS) -I$(KERNEL_DIR) -c -o $@ $<

$(BUILD_DIR)/kernel/%.asm.o: $(KERNEL_DIR)/%.asm | $(BUILD_DIR)/kernel
	@echo "[NASM] $<"
	$(NASM) -w+all -w-reloc -f elf64 -o $@ $<

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_DIR)/kernel.ld
	@echo "[LD]   kernel.elf"
	$(LD) -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
	      -T $(KERNEL_DIR)/kernel.ld -o $@ $(KERNEL_OBJS)
	@echo "[OK]   kernel.elf = $$(stat -c%s $@) bytes"

# =============================================================================
# Disk image: FAT12 floppy with stage1 as boot sector, stage2 in reserved area
# =============================================================================
ROOTFS_FILES := $(wildcard $(ROOTFS_DIR)/*)

$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_ELF) $(ROOTFS_FILES) | $(BUILD_DIR)
	@echo "[IMG]  $@"
	$(DD) if=/dev/zero of=$@ bs=512 count=$(DISK_SECTORS) status=none
	$(DD) if=$(STAGE1_BIN) of=$@ conv=notrunc bs=512 count=1 status=none
	$(DD) if=$(STAGE2_BIN) of=$@ conv=notrunc bs=512 seek=1 status=none
	@# Empty FAT12 tables: entry 0 = media byte, entry 1 = end-of-chain
	printf '\xF0\xFF\xFF' | $(DD) of=$@ conv=notrunc bs=1 \
	    seek=$$(( $(RESERVED_SECTORS) * 512 )) status=none
	printf '\xF0\xFF\xFF' | $(DD) of=$@ conv=notrunc bs=1 \
	    seek=$$(( ($(RESERVED_SECTORS) + $(FAT_SECTORS)) * 512 )) status=none
	@echo "[MCOPY] KERNEL.ELF (debug info stripped; build/kernel.elf keeps it for GDB)"
	objcopy --strip-debug $(KERNEL_ELF) $(BUILD_DIR)/kernel.disk.elf
	mcopy -i $@ $(BUILD_DIR)/kernel.disk.elf ::KERNEL.ELF
	@for f in $(ROOTFS_FILES); do \
	     n=$$(basename $$f | tr a-z A-Z); \
	     echo "[MCOPY] $$n"; mcopy -i $@ $$f ::$$n || exit 1; \
	 done
	@echo "[OK]   $@"

# =============================================================================
# Run / debug
# =============================================================================
.PHONY: run
run: $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS)

.PHONY: run-headless
run-headless: $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS) -display none

.PHONY: run-floppy
run-floppy: $(DISK_IMG)
	$(QEMU) -drive file=$(DISK_IMG),format=raw,if=floppy -boot a \
	        $(QEMU_COMMON) -serial stdio

.PHONY: debug
debug: $(DISK_IMG)
	@echo "[QEMU] Frozen at reset, GDB stub on :1234. In another terminal:"
	@echo "       gdb -x $(SCRIPTS_DIR)/gdbinit"
	$(QEMU) $(QEMU_DEBUG_FLAGS)

.PHONY: test
test: $(DISK_IMG)
	@$(SCRIPTS_DIR)/test_boot.sh $(DISK_IMG)

# =============================================================================
# Inspection
# =============================================================================
.PHONY: hexdump
hexdump: $(STAGE1_BIN)
	@echo "=== stage1.bin (first 64 bytes) ==="
	@xxd -l 64 $(STAGE1_BIN)
	@echo "=== Boot signature (should be 55 aa) ==="
	@xxd -s 510 -l 2 $(STAGE1_BIN)

.PHONY: verify
verify: $(DISK_IMG)
	@sig=$$(xxd -s 510 -l 2 -p $(DISK_IMG)); \
	 if [ "$$sig" = "55aa" ]; then echo "[PASS] Boot signature 0x55AA"; \
	 else echo "[FAIL] Boot signature missing (got: $$sig)"; exit 1; fi
	@mdir -i $(DISK_IMG) ::

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: help
help:
	@echo ""
	@echo "  make all           Build the disk image (default)"
	@echo "  make run           Boot in QEMU (window + serial on stdio)"
	@echo "  make run-headless  Boot in QEMU, serial console only"
	@echo "  make run-floppy    Boot the image as a floppy (drive 0x00)"
	@echo "  make debug         QEMU frozen with GDB stub on :1234"
	@echo "  make test          Automated headless boot test"
	@echo "  make hexdump       Hex dump of stage1.bin"
	@echo "  make verify        Check boot signature and list the FAT12 root"
	@echo "  make clean         Remove build/"
	@echo ""
