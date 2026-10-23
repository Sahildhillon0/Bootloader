/* console.h — kernel console: VGA + serial, printf */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdarg.h>

void console_putc(char c);
void console_puts(const char *s);
int  kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  kvprintf(const char *fmt, va_list ap);

#endif
