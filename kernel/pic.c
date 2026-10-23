/* pic.c — remap the 8259A PICs to vectors 32-47
 *
 * At power-on the master PIC delivers IRQ 0-7 on vectors 8-15, on top of
 * the CPU exceptions. ICW2 moves them to 32-39 (master) and 40-47 (slave).
 */
#include "io.h"
#include "pic.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20
#define PIC_READ_ISR 0x0B

void pic_init(void)
{
    outb(PIC1_CMD, 0x11);  io_wait();   /* ICW1: init, ICW4 follows */
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, 32);   io_wait();   /* ICW2: vector offsets */
    outb(PIC2_DATA, 40);   io_wait();
    outb(PIC1_DATA, 4);    io_wait();   /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 2);    io_wait();
    outb(PIC1_DATA, 0x01); io_wait();   /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFB);              /* mask all but the cascade (IRQ2) */
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(int irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

void pic_mask(int irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (1 << (irq & 7)));
}

void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

bool pic_is_spurious(int irq)
{
    if (irq == 7) {
        outb(PIC1_CMD, PIC_READ_ISR);
        return !(inb(PIC1_CMD) & 0x80);
    }
    if (irq == 15) {
        outb(PIC2_CMD, PIC_READ_ISR);
        if (!(inb(PIC2_CMD) & 0x80)) {
            outb(PIC1_CMD, PIC_EOI);    /* the master did see IRQ2 */
            return true;
        }
    }
    return false;
}
