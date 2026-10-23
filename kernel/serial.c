/* serial.c — COM1 at 115200 8N1, polled */
#include "io.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* IER: no interrupts, we poll */
    outb(COM1 + 3, 0x80);   /* LCR: DLAB=1 */
    outb(COM1 + 0, 0x01);   /* divisor 1 = 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1, DLAB=0 */
    outb(COM1 + 2, 0xC7);   /* FIFO on, cleared, 14-byte threshold */
    outb(COM1 + 4, 0x03);   /* DTR + RTS */
}

void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))     /* LSR: transmit holding empty */
        ;
    outb(COM1, (uint8_t)c);
}

int serial_getc(void)
{
    if (!(inb(COM1 + 5) & 0x01))        /* LSR: data ready */
        return -1;
    return inb(COM1);
}
