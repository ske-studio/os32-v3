/* Real registry, with IF and IRQ entry supplied by the host. */
#define IO_H
#include "types.h"
#include "tick.h"
#include "tick_order.h"
static unsigned int host_if = 0x202;
static unsigned int saves, restores;
static void fail(int line) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 200 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static unsigned int irq_save(void) {
    unsigned int f = host_if; host_if &= ~0x200U; saves++; return f;
}
static void irq_restore(unsigned int f) { CHECK(!(host_if & 0x200U)); host_if = f; restores++; }
#ifndef TICK_SOURCE
#define TICK_SOURCE "../../kernel/irq/tick.c"
#endif
#include TICK_SOURCE
volatile u32 kctx_irq_depth;
static unsigned int log[TICK_SLOT_COUNT], count;
static void record(unsigned int n) {
    CHECK(kctx_irq_depth && !(host_if & 0x200U));
    CHECK(tick_register(n, 0) == TICK_ERR_CTX);
    CHECK(count < TICK_SLOT_COUNT); log[count++] = n;
}
static void snd(void) { record(PC98_TICK_SND); }
static void pcm(void) { record(PC98_TICK_PCM); }
static void ne2k(void) { record(PC98_TICK_NE2K); }
static void e82557(void) { record(PC98_TICK_E82557); }
static void link(void) { record(PC98_TICK_LINK); }
static void lgy98(void) { record(PC98_TICK_LGY98); }
static tick_fn callbacks[] = {snd, pcm, ne2k, e82557, link, lgy98};
static void dispatch(unsigned int expected) {
    count = 0; host_if = 0x2; kctx_irq_depth = 1;
    tick_dispatch(); CHECK(count == expected);
    kctx_irq_depth = 0;
}
void _start(void) {
    CHECK(PC98_TICK_COUNT == TICK_SLOT_COUNT);
    dispatch(0);
    for (unsigned int n = TICK_SLOT_COUNT; n-- > 0;) {
        for (unsigned int flags = 0x2; flags <= 0x202; flags += 0x200) {
            host_if = flags;
            CHECK(!tick_register(n, callbacks[n])); CHECK(host_if == flags);
            CHECK(tick_register(n, callbacks[(n + 1) % TICK_SLOT_COUNT]) == TICK_ERR_BUSY);
            CHECK(host_if == flags);
            CHECK(tick_register(n, 0) == TICK_ERR_INVAL); CHECK(host_if == flags);
            CHECK(tick_register(TICK_SLOT_COUNT, callbacks[n]) == TICK_ERR_INVAL);
            CHECK(tick_register(~0U, callbacks[n]) == TICK_ERR_INVAL); CHECK(host_if == flags);
            kctx_irq_depth = 1;
            CHECK(tick_register(n, callbacks[n]) == TICK_ERR_CTX); CHECK(host_if == flags);
            kctx_irq_depth = 0;
        }
        dispatch(TICK_SLOT_COUNT - n);
        for (unsigned int i = 0; i < count; i++) CHECK(log[i] == n + i);
    }
    dispatch(TICK_SLOT_COUNT);
    for (unsigned int i = 0; i < count; i++) CHECK(log[i] == i);
    CHECK(saves == restores);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
