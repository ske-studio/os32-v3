#include "types.h"
#include "kernel/irq/tick.h"
#define OS32_PPC_ARCH_IO_H
#define EE 0x8000u
static u32 msr;
static int work, waits, ticks, registrations, registration_rc;
static tick_fn callback;
static unsigned int registered_slot;
u32 ppc_irq_save(void) { u32 saved = msr; msr &= ~EE; return saved; }
void ppc_irq_restore(u32 saved) { msr = (msr & ~EE) | (saved & EE); }
static void _enable(void) { msr |= EE; }
static void _halt(void) { ++waits; }
#include LINK_PLATFORM_SOURCE
volatile u32 tick_count;
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) finish(__LINE__); } while (0)
void idle_work_run(void) {
    CHECK(msr & EE);
    ++work;
    /* Bound the harness recursion so removing the guard fails at runtime. */
    if (work == 1) link_platform_idle();
}
void link_tick(void) { ++ticks; }
int tick_register(unsigned int slot, tick_fn fn) {
    ++registrations; registered_slot = slot; callback = fn;
    return registration_rc;
}
void _start(void) {
    tick_count = 0xfffffffdu; CHECK(link_platform_now() == tick_count);
    tick_count = 3; CHECK(link_platform_now() == 3);
    msr = EE | 2;
    u32 outer = link_platform_lock(), inner = link_platform_lock();
    CHECK(!(msr & EE));
    link_platform_unlock(inner); CHECK(!(msr & EE));
    msr |= 4;
    link_platform_unlock(outer); CHECK(msr == (EE | 6));
    msr = 2; outer = link_platform_lock();
    link_platform_unlock(outer); CHECK(msr == 2);
    link_platform_idle(); CHECK(work == 1 && waits == 1 && ticks == 0);
    link_platform_idle(); CHECK(work == 2 && waits == 2);
    registration_rc = TICK_ERR_BUSY;
    CHECK(pm_link_tick_register() == TICK_ERR_BUSY && registrations == 1);
    registration_rc = 0;
    CHECK(pm_link_tick_register() == 0 && registrations == 2);
    CHECK(registered_slot == 4 && callback == link_tick);
    CHECK(pm_link_tick_register() == 0 && registrations == 2);
    callback(); CHECK(ticks == 1);
    finish(0);
}
