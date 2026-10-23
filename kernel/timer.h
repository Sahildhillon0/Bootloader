/* timer.h — PIT channel 0 system tick */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_HZ 100

void     timer_init(void);
uint64_t timer_ticks(void);
void     timer_sleep(uint64_t ms);

#endif
