/* keyboard.c — PS/2 keyboard on IRQ 1, scancode set 1 (US layout)
 *
 * The 8042 controller hands us one byte per key event at port 0x60: the
 * make code on press, make | 0x80 on release, with 0xE0 announcing an
 * extended key. The BIOS has already initialised the controller.
 *
 * Decoded characters go into a ring buffer. input_getc() drains that buffer
 * and also polls COM1, so the shell works on the screen and over serial.
 */
#include <stdbool.h>
#include <stdint.h>
#include "idt.h"
#include "io.h"
#include "keyboard.h"
#include "serial.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

static const char keymap[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ',
};

static const char keymap_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ',
};

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_CAPS   0x3A

static bool shift, ctrl, caps, extended;

#define BUF_SIZE 128
static volatile char buf[BUF_SIZE];
static volatile unsigned head, tail;

static void push(char c)
{
    unsigned next = (head + 1) % BUF_SIZE;
    if (next != tail) {             /* drop keys when full */
        buf[head] = c;
        head = next;
    }
}

static void keyboard_irq(struct regs *r)
{
    (void)r;
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0xE0) {
        extended = true;
        return;
    }
    bool release = sc & 0x80;
    sc &= 0x7F;

    if (extended) {                 /* right Ctrl; arrows etc. are ignored */
        extended = false;
        if (sc == SC_CTRL)
            ctrl = !release;
        return;
    }

    switch (sc) {
    case SC_LSHIFT:
    case SC_RSHIFT:
        shift = !release;
        return;
    case SC_CTRL:
        ctrl = !release;
        return;
    case SC_CAPS:
        if (!release)
            caps = !caps;
        return;
    }
    if (release)
        return;

    char c = shift ? keymap_shift[sc] : keymap[sc];
    if (!c)
        return;
    if (caps && c >= 'a' && c <= 'z' && !shift)
        c -= 32;
    else if (caps && c >= 'A' && c <= 'Z' && shift)
        c += 32;
    if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c &= 0x1F;                  /* Ctrl-C = 3, Ctrl-L = 12, ... */
    push(c);
}

void keyboard_init(void)
{
    while (inb(KBD_STATUS) & 1)     /* drop anything left over from the BIOS */
        inb(KBD_DATA);
    irq_install(1, keyboard_irq);
}

int input_getc(void)
{
    for (;;) {
        cli();
        if (tail != head) {
            char c = buf[tail];
            tail = (tail + 1) % BUF_SIZE;
            sti();
            return (uint8_t)c;
        }
        sti();

        int c = serial_getc();
        if (c >= 0)
            return c == 0x7F ? '\b' : c;   /* terminals send DEL for backspace */

        hlt();                      /* the 100 Hz timer wakes us to poll COM1 */
    }
}
