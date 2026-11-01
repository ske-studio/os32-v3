#define IO_H
#include "tick.h"
#include "tick_order.h"
static unsigned int flags = 0x202, registrations, calls;
static int error;
static tick_fn installed;
static unsigned int irq_save(void) { unsigned int f = flags; flags &= ~0x200U; return f; }
static void irq_restore(unsigned int f) { flags = f; }
int tick_register(unsigned int slot, tick_fn fn) {
    if (slot != PC98_TICK_GF) return -9;
    registrations++;
    if (error) return error;
    installed = fn;
    return 0;
}
#ifndef GF_TICK_SOURCE
#define GF_TICK_SOURCE "../../drivers/video/geforce/geforce_tick.c"
#endif
#include GF_TICK_SOURCE
static void fail(int line) { __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static void poll(void *arg) { CHECK(arg == &calls); calls++; }
void _start(void) {
    error = -3; CHECK(geforce_tick_bind(poll, &calls) == -3); CHECK(!installed);
    error = 0; CHECK(!geforce_tick_bind(poll, &calls)); CHECK(flags == 0x202);
    CHECK(!geforce_tick_bind(poll, &calls)); CHECK(registrations == 3);
    flags = 2; installed(); installed(); CHECK(calls == 2);
    CHECK(!geforce_tick_bind(0, 0)); CHECK(flags == 2); installed(); CHECK(calls == 2);
    CHECK(!geforce_tick_bind(poll, &calls)); installed(); CHECK(calls == 3);
    CHECK(!geforce_tick_bind(0, 0)); installed(); CHECK(calls == 3);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
