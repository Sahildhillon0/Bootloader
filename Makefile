# =============================================================================
# Makefile — x86_64 Bootloader Project
# =============================================================================
# Toolchain requirements:
#   nasm     >= 2.15   (assembler)
#   gcc      >= 10     (cross-compiler, for later C stages)
#   qemu     >= 5.0    (qemu-system-x86_64 or qemu-system-i386)
#   bochs    >= 2.7    (optional alternate emulator)
#   dd, xxd            (standard Linux utilities)
#
# Install on Ubuntu/Debian:
#   sudo apt install nasm gcc qemu-system-x86 bochs bochs-x xxd
# =============================================================================

# --- Toolchain ----------------------------------------------------------------
NASM    := nasm
GCC     := gcc
LD      := ld
QEMU    := qemu-system-x86_64
DD      := dd

# --- Directories --------------------------------------------------------------
BOOT_DIR    := boot
KERNEL_DIR  := kernel
BUILD_DIR   := build
INCLUDE_DIR := include
SCRIPTS_DIR := scripts
DOCS_DIR    := docs

# --- Output files -------------------------------------------------------------
DISK_IMG    := $(BUILD_DIR)/boot.img
STAGE1_BIN  := $(BUILD_DIR)/stage1.bin
STAGE2_BIN  := $(BUILD_DIR)/stage2.bin   # created in Stage 2

# --- QEMU flags ---------------------------------------------------------------
# -drive      : attach our disk image as a raw floppy-style drive
# -m          : RAM in megabytes
# -monitor    : expose QEMU monitor on stdio (Ctrl-A c to enter monitor)
# -serial     : route serial port 0 to stdio (we'll use this for debugging)
# -no-reboot  : stop on triple fault instead of rebooting in a loop
# -d          : trace CPU interrupts and guest errors (remove for quiet mode)
QEMU_FLAGS  := \
    -drive file=$(DISK_IMG),format=raw,index=0,media=disk \
    -m 128M \
    -monitor stdio \
    -serial file:$(BUILD_DIR)/serial.log \
    -no-reboot \
    -d int,cpu_reset \
    -D $(BUILD_DIR)/qemu.log

# QEMU flags for GDB debugging (Stage 4+)
QEMU_DEBUG_FLAGS := \
    $(QEMU_FLAGS) \
    -S \
    -gdb tcp::1234

# =============================================================================
# Default target — build everything available so far
# =============================================================================
.PHONY: all
all: $(DISK_IMG)
	@echo ""
	@echo "Build complete: $(DISK_IMG)"
	@echo "Run with: make run"
	@echo ""

# =============================================================================
# Create build directory
# =============================================================================
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# =============================================================================
# Stage 1: Assemble the boot sector → flat 512-byte binary
# =============================================================================
$(STAGE1_BIN): $(BOOT_DIR)/stage1.asm | $(BUILD_DIR)
	@echo "[NASM] Assembling stage1..."
	$(NASM) -f bin \
	        -o $@ \
	        $<
	@# Sanity check: boot sector must be exactly 512 bytes
	@size=$$(stat -c%s $@); \
	 if [ $$size -ne 512 ]; then \
	     echo "ERROR: stage1.bin is $$size bytes (expected 512)"; \
	     exit 1; \
	 fi
	@echo "[OK]   stage1.bin = $$(stat -c%s $@) bytes"

# =============================================================================
# Disk image construction
# =============================================================================
# We create a 1.44MB floppy image (standard BIOS test target).
#
# Layout:
#   Sector 0 (bytes    0 –  511): Stage 1 (MBR / boot sector)
#   Sector 1 (bytes  512 – 1023): Stage 2 start   ← added in Stage 2
#   Sectors 1–4                 : Stage 2 (4 sectors = 2 KiB)
#   Sector 5+                   : FAT12 filesystem + kernel  ← Stage 3+
#
# 1.44MB = 2880 sectors × 512 bytes
DISK_SECTORS := 2880

$(DISK_IMG): $(STAGE1_BIN) | $(BUILD_DIR)
	@echo "[DD]   Creating blank 1.44MB disk image..."
	$(DD) if=/dev/zero of=$@ bs=512 count=$(DISK_SECTORS) status=none

	@echo "[DD]   Writing stage1 to sector 0..."
	$(DD) if=$(STAGE1_BIN) of=$@ conv=notrunc bs=512 count=1 status=none

	@echo "[OK]   Disk image: $@ ($$(stat -c%s $@) bytes)"

# =============================================================================
# Run in QEMU (no debugger)
# =============================================================================
.PHONY: run
run: $(DISK_IMG)
	@echo "[QEMU] Booting $(DISK_IMG)..."
	$(QEMU) $(QEMU_FLAGS)

# =============================================================================
# Run in QEMU with GDB stub (for debugging, Stage 4+)
# =============================================================================
.PHONY: debug
debug: $(DISK_IMG)
	@echo "[QEMU] Starting with GDB on :1234 (frozen, waiting for GDB)..."
	@echo "       In another terminal: gdb -x scripts/gdbinit"
	$(QEMU) $(QEMU_DEBUG_FLAGS)

# =============================================================================
# Hex dump helpers — inspect raw binary content
# =============================================================================
.PHONY: hexdump
hexdump: $(STAGE1_BIN)
	@echo "=== stage1.bin hex dump (first 64 bytes) ==="
	xxd -l 64 $(STAGE1_BIN)
	@echo ""
	@echo "=== Boot signature (last 2 bytes, should be 55 AA) ==="
	xxd -s 510 -l 2 $(STAGE1_BIN)

.PHONY: hexdump-disk
hexdump-disk: $(DISK_IMG)
	@echo "=== Disk image sector 0 (bytes 0–511) ==="
	xxd -l 512 $(DISK_IMG)

# =============================================================================
# Verify boot signature
# =============================================================================
.PHONY: verify
verify: $(STAGE1_BIN)
	@sig=$$(xxd -s 510 -l 2 -p $(STAGE1_BIN)); \
	 if [ "$$sig" = "55aa" ]; then \
	     echo "[PASS] Boot signature OK: 0x55AA at offset 510"; \
	 else \
	     echo "[FAIL] Boot signature MISSING (got: $$sig)"; \
	     exit 1; \
	 fi

# =============================================================================
# Clean
# =============================================================================
.PHONY: clean
clean:
	@echo "[RM]   Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)

# =============================================================================
# Help
# =============================================================================
.PHONY: help
help:
	@echo ""
	@echo "Bootloader Project — Available Targets"
	@echo "======================================="
	@echo "  make all          Build everything (default)"
	@echo "  make run          Run in QEMU"
	@echo "  make debug        Run in QEMU with GDB stub on :1234"
	@echo "  make hexdump      Hex dump of stage1.bin"
	@echo "  make hexdump-disk Hex dump of sector 0 in disk image"
	@echo "  make verify       Verify 0x55AA boot signature"
	@echo "  make clean        Remove all build output"
	@echo ""
	@echo "Current build stage: 1 (minimal boot sector)"
	@echo ""
