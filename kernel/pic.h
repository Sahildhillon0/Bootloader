/* pic.h — 8259A programmable interrupt controller pair */
#ifndef PIC_H
#define PIC_H

#include <stdbool.h>

void pic_init(void);
void pic_unmask(int irq);
void pic_mask(int irq);
void pic_eoi(int irq);
bool pic_is_spurious(int irq);

#endif
