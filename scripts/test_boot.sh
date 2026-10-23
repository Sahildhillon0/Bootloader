#!/usr/bin/env bash
# =============================================================================
# scripts/test_boot.sh — headless boot test
# =============================================================================
# Boots the image in QEMU with the serial port on stdio, waits for the boot to
# finish, and checks the console output for every expected marker.
#
# Usage: scripts/test_boot.sh build/boot.img
# =============================================================================
set -uo pipefail

IMG=${1:-build/boot.img}
LOG=build/test_serial.log
TIMEOUT=${TIMEOUT:-8}

EXPECT=(
    "[STAGE1] Jumping to stage2"
    "[STAGE2] A20 enabled"
    "[STAGE2] E820 memory map"
    "[STAGE2] Loaded HELLO.TXT"
    "Hello from HELLO.TXT"
    "[STAGE4] 32-bit protected mode"
)

timeout "$TIMEOUT" qemu-system-x86_64 \
    -drive "file=$IMG,format=raw,if=ide,index=0,media=disk" \
    -m 128M -no-reboot -display none -serial stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    </dev/null >"$LOG" 2>&1

fail=0
for marker in "${EXPECT[@]}"; do
    if grep -qF -- "$marker" "$LOG"; then
        echo "[PASS] $marker"
    else
        echo "[FAIL] $marker"
        fail=1
    fi
done

if [ $fail -ne 0 ]; then
    echo "---- serial log ($LOG) ----"
    cat "$LOG"
    exit 1
fi
echo "All boot checks passed."
