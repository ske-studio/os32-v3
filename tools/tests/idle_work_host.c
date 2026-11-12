#define IO_H
#include "types.h"
static int host_if;
static int _irq_enabled(void) { return host_if; }
#ifndef IDLE_WORK_SOURCE
#define IDLE_WORK_SOURCE "../../kernel/idle_work.c"
#endif
#include IDLE_WORK_SOURCE
volatile u32 kctx_irq_depth;
static unsigned int log_items[16], count;
static void fail(int line) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 200 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static void record(unsigned int n) { CHECK(count < 16); log_items[count++] = n; }
static void first(void) { record(1); idle_work_run(); }
static void second(void) { record(2); }
static void third(void) { record(3); }
static void fourth(void) { record(4); }
static void fifth(void) { record(5); }
void _start(void) {
    CHECK(idle_work_register(0) < 0);
    CHECK(idle_work_register(first) == 0);
    CHECK(idle_work_register(second) == 0);
    CHECK(idle_work_register(first) < 0);
    CHECK(idle_work_register(third) == 0);
    CHECK(idle_work_register(fourth) == 0);
    CHECK(idle_work_register(fifth) < 0);
    host_if = 0; idle_work_run(); CHECK(count == 0);
    host_if = 1; kctx_irq_depth = 1; idle_work_run(); CHECK(count == 0);
    kctx_irq_depth = 0; idle_work_run();
    CHECK(count == 4);
    for (unsigned int i = 0; i < 4; ++i) CHECK(log_items[i] == i + 1);
    idle_work_run(); CHECK(count == 8);
    for (unsigned int i = 0; i < 4; ++i) CHECK(log_items[i + 4] == i + 1);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
