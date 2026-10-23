/* timer.c — 8253/8254 PIT channel 0 at TIMER_HZ on IRQ 0
 *
 * The PIT counts down from a divisor at 1.193182 MHz and fires IRQ 0 each
 * time it reaches zero.
 */
#include "idt.h"
#include "io.h"
#include "timer.h"

#define PIT_FREQ    1193182
#define PIT_CH0     0x40
#define PIT_CMD     0x43

static volatile uint64_t ticks;

static void timer_irq(struct regs *r)
{
    (void)r;
    ticks++;
}

void timer_init(void)
{
    uint16_t divisor = PIT_FREQ / TIMER_HZ;
    outb(PIT_CMD, 0x36);            /* channel 0, lo/hi byte, mode 3 */
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, divisor >> 8);
    irq_install(0, timer_irq);
}

uint64_t timer_ticks(void)
{
    return ticks;
}

void timer_sleep(uint64_t ms)
{
    uint64_t end = ticks + (ms * TIMER_HZ + 999) / 1000;
    while (ticks < end)
        hlt();
}
