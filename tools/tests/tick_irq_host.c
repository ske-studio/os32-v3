/* Execute the IRQ0 branch from NASM with port I/O and frame supplied by host. */
static unsigned int steps, handlers, eois, reflects, timeouts;
static unsigned int timeout_armed;
unsigned int frame_flags;
void irq_body(void);
unsigned int tick_clock_step(void) { return ++steps % 10 == 0; }
void timer_handler(void) { handlers++; }
void eoi(void) { eois++; }
void v86_reflect_irq(void *frame, unsigned int irq)
{ (void)frame; if (irq == 0) reflects++; }
int v86_tick_and_check_timeout(void) { timeouts++; return timeout_armed && timeouts == 11; }
void v86_exit_to_kernel(void)
{
    unsigned int failed = steps != 120 || eois != 120 || handlers != 12 ||
                          reflects != 11 || timeouts != 11;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed));
    for (;;) {}
}
void _start(void)
{
    unsigned int i;
    for (i = 1; i <= 100; i++) {
        frame_flags = 0x20000;
        irq_body();
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
