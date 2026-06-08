#!/usr/bin/env bash
# =============================================================================
# scripts/run_qemu.sh — Run the bootloader in QEMU
# =============================================================================
# Usage:
#   ./scripts/run_qemu.sh           Normal run
#   ./scripts/run_qemu.sh --debug   GDB debug mode (waits for GDB on :1234)
#   ./scripts/run_qemu.sh --nographic  Headless (serial only)
# =============================================================================

set -euo pipefail

DISK_IMG="build/boot.img"
LOG_DIR="build"

# Colour output helpers
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# --------------------------------------------------------------------------
# Check prerequisites
# --------------------------------------------------------------------------
check_deps() {
    for cmd in qemu-system-x86_64 nasm make; do
        if ! command -v "$cmd" &>/dev/null; then
            error "Missing required tool: $cmd"
            echo "  Install: sudo apt install qemu-system-x86 nasm"
            exit 1
        fi
    done
}

# --------------------------------------------------------------------------
# Build if disk image is missing or stale
# --------------------------------------------------------------------------
build_if_needed() {
    if [ ! -f "$DISK_IMG" ]; then
        warn "Disk image not found. Building now..."
        make all
    fi
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
check_deps
build_if_needed

mkdir -p "$LOG_DIR"

# Base QEMU arguments
QEMU_ARGS=(
    qemu-system-x86_64
    -drive "file=${DISK_IMG},format=raw,index=0,media=disk"
    -m 128M
    -no-reboot
    -serial "file:${LOG_DIR}/serial.log"
    -D "${LOG_DIR}/qemu.log"
    -d "int,cpu_reset"
)

# Parse arguments
DEBUG_MODE=false
NOGRAPHIC=false

for arg in "$@"; do
    case "$arg" in
        --debug)      DEBUG_MODE=true ;;
        --nographic)  NOGRAPHIC=true  ;;
        *)
            error "Unknown argument: $arg"
            echo "Usage: $0 [--debug] [--nographic]"
            exit 1
            ;;
    esac
done

if $NOGRAPHIC; then
    QEMU_ARGS+=(-nographic -serial stdio)
    info "Running headless (serial on stdio)"
elif $DEBUG_MODE; then
    QEMU_ARGS+=(-S -gdb tcp::1234 -monitor stdio)
    info "GDB debug mode: QEMU frozen at startup"
    info "In another terminal run: gdb -x scripts/gdbinit"
    info "QEMU monitor available via stdio (Ctrl-A c to toggle)"
else
    QEMU_ARGS+=(-monitor stdio)
    info "Running in display mode (Ctrl-A c = QEMU monitor)"
fi

info "Disk image : $DISK_IMG"
info "Serial log : ${LOG_DIR}/serial.log"
info "QEMU log   : ${LOG_DIR}/qemu.log"
echo ""

# --------------------------------------------------------------------------
# Tail serial log in background if not headless
# --------------------------------------------------------------------------
if ! $NOGRAPHIC; then
    info "Serial output will be in: build/serial.log"
    info "Watch it with: tail -f build/serial.log"
fi

exec "${QEMU_ARGS[@]}"
