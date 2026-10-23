#!/usr/bin/env bash
# =============================================================================
# scripts/test_boot.sh — headless boot test
# =============================================================================
# Boots the image in QEMU with the serial port logged to a file and the QEMU
# monitor on a pipe. After the boot settles it dumps the VGA text buffer
# through the monitor, so both outputs are checked:
#
#   serial  every marker in SERIAL_EXPECT must appear on COM1
#   screen  every marker in VGA_EXPECT must be visible in 0xB8000
#
# Usage: scripts/test_boot.sh build/boot.img
# =============================================================================
set -uo pipefail

IMG=${1:-build/boot.img}
SERIAL_LOG=build/test_serial.log
VGA_DUMP=build/test_vga.txt
BOOT_WAIT=${BOOT_WAIT:-4}

SERIAL_EXPECT=(
    "[STAGE1] Jumping to stage2"
    "[STAGE2] A20 enabled"
    "[STAGE2] E820 memory map"
    "[STAGE3] Loaded KERNEL.ELF"
    "[STAGE4] 32-bit protected mode"
    "[STAGE5] 64-bit long mode"
    "[STAGE6] ELF segments loaded"
    "[KERNEL] Hello from the 64-bit C kernel"
    "[KERNEL] Usable RAM"
    "[IDT] Breakpoint at RIP"
    "[KERNEL] PIT running at 100 Hz"
)

# Lines written by the real-mode, protected-mode, long-mode and C printers
VGA_EXPECT=(
    "[STAGE2] A20 enabled"
    "[STAGE4] 32-bit protected mode"
    "[STAGE5] 64-bit long mode"
    "[KERNEL] Hello from the 64-bit C kernel"
)

mkdir -p build
{
    sleep "$BOOT_WAIT"
    echo "xp /2000hx 0xb8000"
    sleep 0.5
    echo "quit"
} | timeout $((BOOT_WAIT + 5)) qemu-system-x86_64 \
    -drive "file=$IMG,format=raw,if=ide,index=0,media=disk" \
    -m 128M -no-reboot -display none \
    -serial "file:$SERIAL_LOG" -monitor stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    2>/dev/null \
| python3 -c '
import re, sys
cells = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{4})\b", sys.stdin.read())]
text = "".join(chr(c & 0xFF) if 32 <= (c & 0xFF) < 127 else " " for c in cells[:2000])
print("\n".join(text[i:i + 80].rstrip() for i in range(0, len(text), 80)))
' >"$VGA_DUMP"

fail=0
check() {    # check <label> <file> <marker>
    if grep -qF -- "$3" "$2"; then
        echo "[PASS] $1: $3"
    else
        echo "[FAIL] $1: $3"
        fail=1
    fi
}
for m in "${SERIAL_EXPECT[@]}"; do check serial "$SERIAL_LOG" "$m"; done
for m in "${VGA_EXPECT[@]}";    do check screen "$VGA_DUMP"   "$m"; done

if [ $fail -ne 0 ]; then
    echo "---- serial log ($SERIAL_LOG) ----"; cat "$SERIAL_LOG"
    echo "---- screen ($VGA_DUMP) ----";       cat "$VGA_DUMP"
    exit 1
fi
echo "All boot checks passed."
