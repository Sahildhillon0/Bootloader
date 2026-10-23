/* serial.h — COM1 (16550 UART) */
#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_putc(char c);
int  serial_getc(void);   /* -1 if no byte is waiting */

#endif
