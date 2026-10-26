/* Real PIC and IRQ policy, deterministic reads and full operation trace. */
#include "io.h"
#include "idt.h"
#include "irq.h"
#include "platform_irq.h"
static unsigned int enabled, read_port, read_value, read_left;
volatile u32 kctx_irq_depth, tick_count;
static void event(unsigned int op, unsigned int port, unsigned int value)
{
    unsigned int row[4] = { op, port, value, enabled };
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(row), "d"(sizeof(row)) : "memory");
}
static void finish(unsigned int rc) __attribute__((noreturn));
static void finish(unsigned int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
static void check(int ok) { if (!ok) finish(10); }
static void input(unsigned int port, unsigned int value)
{
    check(!read_left);
    read_port = port; read_value = value; read_left = 1;
}
void outp(unsigned int port, unsigned int value) { event('W', port, value); }
unsigned int inp(unsigned int port)
{
    check(read_left && read_port == port);
    read_left = 0; event('R', port, read_value); return read_value;
}
unsigned int irq_save(void)
{
    unsigned int old = enabled;
    event('S', 0, old); enabled = 0; return old;
}
void irq_restore(unsigned int flags) { event('F', 0, flags); enabled = flags; }
void io_wait(void) { event('T', 0, 0); }
void io_wait_n(int n) { event('T', 0, (unsigned int)n); }
void isr_unexpected_report(u32 irq) { event('U', irq, 0); }
void kprintf(int attr, const char *fmt, ...) { (void)attr; (void)fmt; event('K', 0, 0); }
static int handler(unsigned int irq, void *arg) { (void)irq; (void)arg; return IRQ_HANDLED; }
void _start(void)
{
    unsigned int irq, state, mask, handled, quiet, bit;
    pic_init();
    for (state = 0; state < 2; state++) {
        for (irq = 0; irq < IRQ_LINE_MAX; irq++) {
            for (mask = 0; mask <= 255; mask += 85) {
                unsigned int port = irq < PIC_IRQS ? PIC1_DATA : PIC2_DATA;
                enabled = state;
                event('C', irq, mask);
                input(port, mask); irq_enable(irq); check(enabled == state);
                input(port, mask); irq_disable(irq); check(enabled == state);
            }
        }
    }
    enabled = 0;
    /* C finish only. IRQ0/7 ASM behavior remains an N/R acceptance item. */
    for (irq = 0; irq < IRQ_LINE_MAX; irq++) {
        for (handled = 0; handled < 2; handled++) {
            for (quiet = 0; quiet < 2; quiet++) {
                for (bit = 0; bit < 2; bit++) {
                    event('C', irq, handled | quiet << 1 | bit << 2);
                    irq_test_quiet = quiet;
                    if (irq == PIC_SLAVE_LAST_IRQ) input(PIC2_CMD, bit ? 0x80 : 0x7f);
                    irq_finish(irq, handled);
                    check(!read_left && !enabled);
                }
            }
        }
    }
    /* Timer pending is a standalone L12 boundary test, without touching ktime. */
    for (mask = 0; mask <= 255; mask += 85) {
        input(PIC1_CMD, mask);
        check(platform_irq_timer_pending() == (int)(mask & 1u));
    }
    /* Actual registration / recount / quarantine path, not only math helpers. */
    irq_test_quiet = 1; enabled = 1;
    input(PIC2_DATA, 255);
    check(irq_register(8, handler, 0, 0) == 0);
    check(irq_lines[irq_dyn_index(8)].count == 1);
    enabled = 0; irq_dispatch(8); enabled = 1;
    input(PIC2_DATA, 254); check(irq_quarantine_line(8) == 0);
    check(irq_register(8, handler, 0, 0) == IRQ_ERR_BUSY);
    input(PIC2_DATA, 255); check(irq_unregister(8, handler, 0) == 0);
    check(irq_lines[irq_dyn_index(8)].count == 0);
    check(irq_lines[irq_dyn_index(8)].quarantined && enabled == 1);
    check(!read_left);
    finish(0);
}
