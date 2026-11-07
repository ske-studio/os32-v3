/* Real core, policy and PIC with only port I/O replaced; alternate 64-line provider. */
#include "support.h"
#include "platform_irq.h"
#include "irq.h"
#include "pc98.h"
static unsigned events[1024], n, enabled, slave_isr;
static int reply;
static void event(unsigned e) { CHECK(n < 1024); events[n++] = e; }
#ifndef MOCK64
#include "idt.h"
void outp(unsigned port, unsigned value) {
    if (value == OCW2_EOI && port == PIC1_CMD) event(4);
    if (value == OCW2_EOI && port == PIC2_CMD) event(3);
}
unsigned inp(unsigned port) { return port == PIC2_CMD ? slave_isr : 0xff; }
#endif
unsigned irq_save(void) { unsigned old = enabled; enabled = 0; return old; }
void irq_restore(unsigned flags) { enabled = flags; }
void _stop(void) { die(2); }
void kprintf(int attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void isr_unexpected_report(u32 irq) { (void)irq; event(5); }
volatile u32 tick_count, kctx_irq_depth;
static int handler(unsigned irq, void *arg) {
    (void)irq; (void)arg; CHECK(!enabled); event(2); return reply;
}
#ifdef MOCK64
static unsigned dynamic[63];
static struct irq_topology topology = {64, 63, dynamic};
const struct irq_topology *platform_irq_topology(void) { return &topology; }
int platform_irq_reserved(unsigned line) { return line == 0; }
void platform_irq_mask(unsigned line) { CHECK(line < 64 && !enabled); }
void platform_irq_unmask(unsigned line) { CHECK(line < 64 && !enabled); }
int platform_irq_claim(unsigned token, struct irq_claim *out) {
    event(1);
    if (!out || (token >= 64 && token != 255)) return -1;
    *out = (struct irq_claim){token, token, token != 255, token != 255};
    return 1;
}
void platform_irq_complete(const struct irq_claim *c) {
    CHECK(c->needs_finish && c->native_token == c->line); event(4);
}
#endif
static void expect(const unsigned *want, unsigned count) {
    CHECK(n == count);
    for (unsigned i = 0; i < n; i++) CHECK(events[i] == want[i]);
    n = 0;
}
void _start(void) {
#ifdef MOCK64
    for (unsigned i = 0; i < 63; i++) dynamic[i] = i + 1;
#endif
    irq_topology_check();
    CHECK(irq_register(PLATFORM_IRQ_LINES, handler, 0, 0) == IRQ_ERR_INVAL);
    CHECK(irq_register(0, handler, 0, 0) == IRQ_ERR_NOTSUP);
#ifdef MOCK64
    const unsigned handled[] = {1,2,2,4}, empty[] = {1,4,5}, spurious[] = {1};
    reply = IRQ_HANDLED;
    CHECK(irq_register(32, handler, 0, 0) == 0);
    irq_dispatch(32); expect(handled, 4);
    CHECK(irq_quarantine_line(31) == 0);
    CHECK(irq_line_quarantined == 0x80000000u);
    CHECK(irq_quarantine_line(32) == 0);
    CHECK(irq_line_quarantined == 0x80000000u);
    CHECK(irq_lines[irq_dyn_index(32)].quarantined);
    CHECK(irq_register(32, handler, (void *)1, 0) == IRQ_ERR_BUSY);
    irq_test_reset_line(32);
    CHECK(irq_line_quarantined == 0x80000000u);
    irq_test_reset_line(31); CHECK(irq_line_quarantined == 0);
    irq_test_quiet = 1;
    for (unsigned i = 0; i <= IRQ_STORM_LIMIT; i++) { n = 0; irq_dispatch(63); }
    CHECK(irq_lines[irq_dyn_index(63)].storm_masked && irq_storm_masked == 0);
    irq_test_reset_line(63);
    CHECK(!irq_lines[irq_dyn_index(63)].storm_masked);
    irq_test_quiet = 0; n = 0;
    irq_dispatch(255); expect(spurious, 1); /* OpenPIC-like no delivery or completion. */
    irq_dispatch(64); expect(spurious, 1); /* Invalid claim: no dispatch/complete/report. */
    irq_dispatch(30); expect(empty, 3);
#else
    struct irq_claim c = {99,88,77,66};
    CHECK(platform_irq_claim(16, &c) < 0 && c.line == 99 && c.needs_finish == 66);
    CHECK(platform_irq_claim(3, 0) < 0);
    for (unsigned i = 0; i < 16; i++) {
        if (platform_irq_reserved(i)) {
            CHECK(platform_irq_claim(i, &c) < 0 && c.line == 99);
            CHECK(irq_register(i, handler, 0, 0) == IRQ_ERR_NOTSUP);
            irq_dispatch(i); CHECK(n == 0); /* fixed 0 and 7 retain stub ownership */
            irq_finish(i, 0); CHECK(n == 0);
        }
    }
    CHECK(platform_irq_claim(15, &c) == 1 && c.deliver && c.needs_finish);
    CHECK(irq_register(15, handler, 0, 0) == 0);
    for (unsigned bit = 0; bit < 2; bit++) {
        slave_isr = bit ? 0x80 : 0;
        reply = IRQ_HANDLED;
        const unsigned a[] = {2,2,3,4}, b[] = {2,2,4};
        irq_dispatch(15); expect(bit ? a : b, bit ? 4 : 3);
        reply = IRQ_NONE;
        const unsigned d[] = {2,3,4,5}, e[] = {2,4,5};
        irq_dispatch(15); expect(bit ? d : e, bit ? 4 : 3);
    }
    const unsigned finish[] = {3,4,5};
    irq_finish(15, 0); expect(finish, 3);
    CHECK(irq_register(3, handler, 0, 0) == 0);
    reply = IRQ_NONE;
    const unsigned master[] = {2,4,5};
    irq_dispatch(3); expect(master, 3);
#endif
    say("PASS irq_controller: topology / dispatch-complete-report / reserved / spurious / projection\n");
    die(0);
}
