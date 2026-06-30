// ============================================================
// timer.c - Temporizador del sistema NyxOS (PIT a 1000 Hz, IRQ)
// ============================================================
#include "kernel.h"

static uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 1000;
static uint32_t pit_divisor = 0;

void init_timer(uint32_t frequency) {
    timer_frequency = frequency;
    timer_ticks = 0;
    pit_divisor = 1193180 / frequency;

    // Configurar PIT en modo 2 (rate generator, IRQ on terminal count)
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(pit_divisor & 0xFF));
    outb(0x40, (uint8_t)((pit_divisor >> 8) & 0xFF));

    printf("[TIMER] %d Hz (interrupt-driven)\n", frequency);
}

uint32_t get_ticks(void) {
    return tick_count;
}

void sleep(uint32_t milliseconds) {
    uint32_t start = tick_count;
    uint32_t ticks_to_wait = (milliseconds * timer_frequency) / 1000;
    while ((tick_count - start) < ticks_to_wait) {
        __asm__ volatile("nop");
    }
}