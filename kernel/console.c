/* console.c — every byte goes to the VGA screen and to COM1
 *
 * kprintf supports: %c %s %d %i %u %x %X %p %%, the l/ll/z length
 * modifiers, a field width and the '0' and '-' flags.
 */
#include <stdint.h>
#include <stdbool.h>
#include "console.h"
#include "serial.h"
#include "vga.h"

void console_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
    vga_putc(c);
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
}

static int pad(int n, char c)
{
    int i;
    for (i = 0; i < n; i++)
        console_putc(c);
    return i;
}

static int put_number(uint64_t v, unsigned base, bool upper, bool negative,
                      int width, bool zero, bool left)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[24];
    int len = 0, out = 0;

    do {
        buf[len++] = digits[v % base];
        v /= base;
    } while (v);

    int total = len + negative;
    if (!left && !zero)
        out += pad(width - total, ' ');
    if (negative)
        console_putc('-'), out++;
    if (!left && zero)
        out += pad(width - total, '0');
    while (len)
        console_putc(buf[--len]), out++;
    if (left)
        out += pad(width - total, ' ');
    return out;
}

int kvprintf(const char *fmt, va_list ap)
{
    int out = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            console_putc(*fmt);
            out++;
            continue;
        }
        fmt++;

        bool zero = false, left = false;
        for (;; fmt++) {
            if (*fmt == '0')
                zero = true;
            else if (*fmt == '-')
                left = true;
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int longs = 0;
        while (*fmt == 'l' || *fmt == 'z') {
            longs++;
            fmt++;
        }

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left)
                out += pad(width - 1, ' ');
            console_putc(c);
            out++;
            if (left)
                out += pad(width - 1, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = 0;
            while (s[len])
                len++;
            if (!left)
                out += pad(width - len, ' ');
            console_puts(s);
            out += len;
            if (left)
                out += pad(width - len, ' ');
            break;
        }
        case 'd':
        case 'i': {
            int64_t v = longs ? va_arg(ap, int64_t) : va_arg(ap, int);
            bool neg = v < 0;
            out += put_number(neg ? -(uint64_t)v : (uint64_t)v, 10, false,
                              neg, width, zero, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t v = longs ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            out += put_number(v, *fmt == 'u' ? 10 : 16, *fmt == 'X', false,
                              width, zero, left);
            break;
        }
        case 'p':
            console_puts("0x");
            out += 2 + put_number((uintptr_t)va_arg(ap, void *), 16, false,
                                  false, 16, true, false);
            break;
        case '%':
            console_putc('%');
            out++;
            break;
        case '\0':
            return out;
        default:
            console_putc('%');
            console_putc(*fmt);
            out += 2;
        }
    }
    return out;
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}
