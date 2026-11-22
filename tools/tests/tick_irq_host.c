/* Execute the IRQ0 branch from NASM with port I/O and frame supplied by host. */
#include "tick_clock.h"
#include "tick_probe_ids.h"
static unsigned int handlers, eois, reflects, timeouts;
static unsigned int timeout_armed;
unsigned int frame_flags;
void irq_body(void);
static void require(int ok)
{
    if (!ok)
        __asm__ volatile("int $0x80" : : "a"(1), "b"(42));
}
static unsigned int current = TP_METRICS;
static unsigned int samples[TP_METRICS];
void probe_begin(unsigned int id)
{
    require(current == TP_METRICS && id < TP_METRICS);
    current = id;
}
void probe_end(unsigned int id)
{
    require(current == id && id < TP_METRICS);
    ++samples[id];
    current = TP_METRICS;
}
static unsigned int notifications;
static void notify(u32 now)
{
    require(now == eois + 1 && now == notifications + 1);
    ++notifications;
}
void timer_handler(void) { require(current == TP_TIMER && eois + 1 == tick_clock_ms()); handlers++; }
void eoi(void) { require(current == TP_EOI); ++eois; require(notifications == eois); require(eois == tick_clock_ms() && handlers == eois / 10); }
void v86_reflect_irq(void *frame, unsigned int irq)
{ (void)frame; require(current == TP_REFLECT); require(eois == tick_clock_ms() && eois % 10 == 0); if (irq == 0) reflects++; }
int v86_tick_and_check_timeout(void) { require(current == TP_TIMEOUT); ++timeouts; require(timeouts == reflects); return timeout_armed && timeouts == 11; }
void v86_exit_to_kernel(void)
{
    require(current == TP_METRICS && samples[TP_TIMEOUT] == 12);
    unsigned int failed = tick_clock_ms() != 120 || eois != 120 || handlers != 12 ||
                          reflects != 11 || timeouts != 11;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed));
    for (;;) {}
}
void _start(void)
{
    unsigned int i;
    require(tick_clock_config(1, 1000) == 0);
    require(tick_fast_set(notify) == 0);
    for (i = 1; i <= 100; i++) {
        frame_flags = 0x20000;
        irq_body();
        require(samples[TP_EOI] == i && samples[TP_TIMER] == i / 10 &&
                samples[TP_REFLECT] == i / 10 && samples[TP_TIMEOUT] == i / 10 &&
                samples[TP_SLOT7] == 0);
        if (eois != i || handlers != i / 10 || reflects != i / 10 || timeouts != i / 10)
            __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
    }
    frame_flags = 0;
    for (i = 0; i < 10; i++) irq_body();
    if (eois != 110 || handlers != 11 || reflects != 10 || timeouts != 10)
        __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
    frame_flags = 0x20000;
    timeout_armed = 1;
    for (i = 0; i < 10; i++) irq_body();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1));
    for (;;) {}
}
