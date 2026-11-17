#define IO_H
#include "types.h"
static int host_if, receive_in_work;
static unsigned int sleeps, recent_ms = 5;
static void _idle(void) { ++sleeps; }
int net_rx_recent(u32 window_ms) { return recent_ms < window_ms; }
static int _irq_enabled(void) { return host_if; }
#ifndef IDLE_WORK_SOURCE
#define IDLE_WORK_SOURCE "../../kernel/idle_work.c"
#endif
#include IDLE_WORK_SOURCE
#include "io_clock.h"
static unsigned int clock_reads;
static int read_clock(u32 *a, u32 *b, u32 *c) {
    (void)a; (void)b; (void)c; ++clock_reads; return OS32_ERR_NOSYS;
}
static const struct io_clock_ops ops = { .read = read_clock };
volatile u32 kctx_irq_depth;
static unsigned int log_items[16], count;
static void fail(int line) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 200 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static void record(unsigned int n) { CHECK(count < 16); log_items[count++] = n; }
static void first(void) { record(1); if (receive_in_work) recent_ms = 0; idle_work_run(); }
static void second(void) { record(2); }
static void third(void) { record(3); }
static void fourth(void) { record(4); }
static void fifth(void) { record(5); }
void _start(void) {
    host_if = 1;
    io_clock_set_ops(&ops);
    idle_work_run(); CHECK(clock_reads == 0);
    io_clock_set_ops(0);
    recent_ms = 0; idle_wait_step(); CHECK(sleeps == 0);
    recent_ms = 4; idle_wait_step(); CHECK(sleeps == 0);
    recent_ms = 5; idle_wait_step(); CHECK(sleeps == 1);
    host_if = 0; idle_wait_step(); CHECK(sleeps == 1 && !host_if);
    host_if = 1; kctx_irq_depth = 1; idle_wait_step(); CHECK(sleeps == 1);
    kctx_irq_depth = 0;
    CHECK(idle_work_register(0) == OS32_ERR_INVAL);
    CHECK(idle_work_register(first) == 0);
    CHECK(idle_work_register(second) == 0);
    CHECK(idle_work_register(first) == OS32_ERR_EXIST);
    CHECK(idle_work_register(third) == 0);
    CHECK(idle_work_register(fourth) == 0);
    CHECK(idle_work_register(fifth) == OS32_ERR_FULL);
    host_if = 0; idle_work_run(); CHECK(count == 0);
    host_if = 1; kctx_irq_depth = 1; idle_work_run(); CHECK(count == 0);
    kctx_irq_depth = 0; idle_work_run();
    CHECK(count == 4);
    for (unsigned int i = 0; i < 4; ++i) CHECK(log_items[i] == i + 1);
    idle_work_run(); CHECK(count == 8);
    for (unsigned int i = 0; i < 4; ++i) CHECK(log_items[i + 4] == i + 1);
    recent_ms = 5; receive_in_work = 1;
    idle_wait_step(); CHECK(count == 12 && sleeps == 1);
    receive_in_work = 0; recent_ms = 5;
    idle_wait_step(); CHECK(count == 16 && sleeps == 2);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
