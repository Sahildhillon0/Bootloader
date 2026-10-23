#!/usr/bin/env python3
"""
scripts/test_boot.py - end-to-end boot and shell test

Boots build/boot.img in QEMU with COM1 on a pipe and the QEMU monitor on a
UNIX socket, then:

  1. waits for every boot marker on the serial console
  2. reads the VGA text buffer (0xB8000) and checks what is on screen
  3. drives the shell over serial (help, ls, cat, disk, mem, uptime, echo)
  4. types a command on the PS/2 keyboard with the monitor's sendkey
  5. runs 'shutdown' and expects QEMU to exit
  6. boots again and checks that 'crash' reaches the page fault handler
  7. boots the image as a floppy (no ATA disk) and checks the shell still runs

Usage: scripts/test_boot.py [build/boot.img]
"""
import os
import re
import socket
import subprocess
import sys
import threading
import time

IMG = sys.argv[1] if len(sys.argv) > 1 else "build/boot.img"
BUILD = os.path.dirname(IMG) or "."
PROMPT = "boot> "

BOOT_MARKERS = [
    "[STAGE1] Jumping to stage2",
    "[STAGE2] A20 enabled",
    "[STAGE2] E820 memory map",
    "[STAGE3] Loaded KERNEL.ELF",
    "[STAGE4] 32-bit protected mode",
    "[STAGE5] 64-bit long mode",
    "[STAGE6] ELF segments loaded",
    "[KERNEL] Hello from the 64-bit C kernel",
    "[KERNEL] Usable RAM",
    "[IDT] Breakpoint at RIP",
    "[KERNEL] PIT running at 100 Hz",
    "[KERNEL] PS/2 keyboard on IRQ 1",
    "[KERNEL] ATA disk: QEMU HARDDISK",
    "[KERNEL] FAT12 volume mounted",
]

# Lines from the protected-mode, long-mode and C printers that must still be
# on screen at the first prompt (earlier lines may have scrolled away)
SCREEN_MARKERS = ["[STAGE4]", "[STAGE5] 64-bit long mode",
                  "[KERNEL] Hello from the 64-bit C kernel", PROMPT.strip()]

SHELL_STEPS = [  # command, text that must appear in its output
    ("help", "shutdown"),
    ("ls", "KERNEL.ELF"),
    ("ls", "HELLO.TXT"),
    ("cat hello.txt", "Hello from HELLO.TXT on the FAT12 volume!"),
    ("cat nope.txt", "cat: nope.txt: not found"),
    ("disk", "Model:   QEMU HARDDISK"),
    ("mem", "Usable:"),
    ("uptime", "ticks at 100 Hz"),
    ("echo serial works", "serial works"),
    ("frobnicate", "frobnicate: unknown command"),
]

failures = 0


def report(ok, what):
    global failures
    print(("[PASS] " if ok else "[FAIL] ") + what)
    if not ok:
        failures += 1


class Machine:
    """One QEMU instance: serial on stdin/stdout, monitor on a socket."""

    def __init__(self, drive):
        self.sock_path = os.path.join(BUILD, "test-monitor.sock")
        if os.path.exists(self.sock_path):
            os.unlink(self.sock_path)
        self.proc = subprocess.Popen(
            ["qemu-system-x86_64", *drive,
             "-m", "128M", "-no-reboot", "-display", "none",
             "-serial", "stdio",
             "-monitor", f"unix:{self.sock_path},server,nowait",
             "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
        self.out = ""
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()
        self.mon = None

    def _reader(self):
        while True:
            b = self.proc.stdout.read1(4096)
            if not b:
                return
            with self.lock:
                self.out += b.decode("latin-1").replace("\r", "")

    def text(self):
        with self.lock:
            return self.out

    def wait_for(self, s, timeout=10.0, start=0):
        end = time.time() + timeout
        while time.time() < end:
            if s in self.text()[start:]:
                return True
            time.sleep(0.05)
        return False

    def send(self, line):
        # A short gap per byte keeps us under the UART's 16-byte FIFO
        for ch in line + "\r":
            self.proc.stdin.write(ch.encode())
            self.proc.stdin.flush()
            time.sleep(0.005)

    def run(self, cmd, timeout=5.0):
        """Send a command, return its output (up to the next prompt)."""
        start = len(self.text())
        self.send(cmd)
        self.wait_for(PROMPT, timeout, start + len(cmd))
        return self.text()[start:]

    def monitor(self, cmd):
        if self.mon is None:
            for _ in range(50):
                try:
                    self.mon = socket.socket(socket.AF_UNIX)
                    self.mon.connect(self.sock_path)
                    break
                except OSError:
                    time.sleep(0.1)
            self.mon.settimeout(0.5)
            self._drain()
        self.mon.sendall((cmd + "\n").encode())
        time.sleep(0.3)
        return self._drain()

    def _drain(self):
        data = b""
        try:
            while True:
                chunk = self.mon.recv(65536)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        return data.decode("latin-1")

    def screen(self):
        dump = self.monitor("xp /2000hx 0xb8000")
        cells = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{4})\b", dump)]
        chars = "".join(chr(c & 0xFF) if 32 <= (c & 0xFF) < 127 else " "
                        for c in cells[:2000])
        return "\n".join(chars[i:i + 80].rstrip()
                         for i in range(0, len(chars), 80))

    def sendkeys(self, text):
        names = {" ": "spc", "\n": "ret", ".": "dot"}
        for ch in text:
            self.monitor("sendkey " + names.get(ch, ch))

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()
        if self.mon:
            self.mon.close()


IDE = ["-drive", f"file={IMG},format=raw,if=ide,index=0,media=disk"]
FLOPPY = ["-drive", f"file={IMG},format=raw,if=floppy", "-boot", "a"]

# --- 1-5: full boot, screen, shell over serial, keyboard, shutdown -----------
m = Machine(IDE)
try:
    report(m.wait_for(PROMPT, 15), "shell prompt reached")
    for marker in BOOT_MARKERS:
        report(marker in m.text(), "serial: " + marker)

    screen = m.screen()
    with open(os.path.join(BUILD, "test_vga.txt"), "w") as f:
        f.write(screen + "\n")
    for marker in SCREEN_MARKERS:
        report(marker in screen, "screen: " + marker)

    for cmd, expect in SHELL_STEPS:
        report(expect in m.run(cmd), f"shell: {cmd!r} -> {expect!r}")

    report("Hello from HELLO.TXT" in m.screen(), "screen: cat output on VGA")

    start = len(m.text())
    m.sendkeys("echo kbd\n")
    report(m.wait_for("echo kbd\nkbd\n", 5, start), "keyboard: PS/2 'echo kbd'")

    m.send("shutdown")
    try:
        m.proc.wait(timeout=5)
        report(True, "shutdown: QEMU powered off")
    except subprocess.TimeoutExpired:
        report(False, "shutdown: QEMU powered off")
finally:
    log = m.text()
    m.close()
    with open(os.path.join(BUILD, "test_serial.log"), "w") as f:
        f.write(log)

# --- 6: exception handler -----------------------------------------------------
m = Machine(IDE)
try:
    m.wait_for(PROMPT, 15)
    m.send("crash")
    report(m.wait_for("*** CPU EXCEPTION 14: Page fault ***", 5),
           "crash: page fault reported")
    report(m.wait_for("CR2=0000000040000000", 2), "crash: CR2 is the bad address")
finally:
    m.close()

# --- 7: floppy boot, no ATA disk ----------------------------------------------
m = Machine(FLOPPY)
try:
    report(m.wait_for(PROMPT, 15), "floppy: shell prompt reached")
    report("boot drive 0x0000" in m.text(), "floppy: booted from drive 0x00")
    report("[KERNEL] No ATA disk" in m.text(), "floppy: no ATA disk detected")
    report("no ATA disk" in m.run("ls"), "floppy: ls explains the missing disk")
finally:
    m.close()

if failures:
    print(f"{failures} check(s) failed. Serial log: {BUILD}/test_serial.log")
    sys.exit(1)
print("All boot checks passed.")
