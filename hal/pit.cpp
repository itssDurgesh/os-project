// The Programmable Interval Timer: the tick that drives the scheduler.

#include "hal.h"
#include "process.h"

#define PIT_COMMAND   0x43
#define PIT_CHANNEL0  0x40
#define PIT_SQUARE    0x36  // channel 0, low then high byte, square wave
#define PIT_BASE_HZ   1193182
#define TIMER_IRQ     0

static volatile uint32_t ticks;

static void timer_interrupt(Registers *r)
{
    (void)r;
    ticks = ticks + 1;
    scheduler_tick();
}

void timer_init(uint32_t hz)
{
    uint32_t divisor = PIT_BASE_HZ / hz;

    outb(PIT_COMMAND, PIT_SQUARE);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    set_interrupt_handler(VECTOR_IRQ_BASE + TIMER_IRQ, timer_interrupt);
    pic_unmask(TIMER_IRQ);
}

uint32_t get_ticks()
{
    return ticks;
}
