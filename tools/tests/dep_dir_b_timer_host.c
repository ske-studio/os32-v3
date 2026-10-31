/* Same harness for fixed baseline capture and the platform implementation. */
#include "io.h"
#include "idt.h"
#include "sysclk.h"
#include "ktime.h"
#include "v86_bios.h"
#include "os32_kapi_shared.h"
static unsigned int enabled, read_at, read_n, reads[4 * TIME_RETRY_MAX];
static unsigned int clocks, tick_value;
static u8 lowmem[BIOS_WORK_SYSCLK + 1];
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
const u8 *v86_bios_real_lowmem(void)
{
    clocks++; event('B', BIOS_WORK_SYSCLK, lowmem[BIOS_WORK_SYSCLK]); return lowmem;
}
static volatile u32 tick_storage;
static volatile u32 *read_tick(void)
{
    event('K', 0, tick_value); tick_storage = tick_value; return &tick_storage;
}
void outp(unsigned int port, unsigned int value)
{
    check(!enabled);
    /* Every programming write must see the already published state. */
    if (port == PIT_MODE && value == PIT_MODE_TIMER0) {
        const struct pit_setup *s = pit_get_setup();
        check(s->valid && s->mode == PIT_MODE_TIMER0);
        event('P', s->reload, s->period_us);
    }
    event('W', port, value);
}
unsigned int inp(unsigned int port)
{
    unsigned int i = read_at++;
    check(!enabled && i < read_n);
    check(port == ((i % 4 == 0 || i % 4 == 3) ? PIC1_CMD : PIT_CNTR0));
    event('R', port, reads[i]); return reads[i];
}
unsigned int irq_save(void)
{
    unsigned int old = enabled; event('S', 0, old); enabled = 0; return old;
}
void irq_restore(unsigned int flags) { event('F', 0, flags); enabled = flags; }
void io_wait(void) { event('T', 0, 0); }
void io_wait_n(int n) { event('T', 0, (unsigned int)n); }
#include "clock_source.inc"
#include "pit_source.inc"
/* Instrument the real volatile tick read, after idt.h has declared its symbol. */
#define tick_count (*read_tick())
#include "ktime_source.inc"
#undef tick_count
static void samples(unsigned int p1, unsigned int count, unsigned int p2, unsigned int n)
{
    unsigned int i;
    check(read_at == read_n); read_at = 0; read_n = 4 * n;
    for (i = 0; i < n; i++) {
        reads[4*i] = p1; reads[4*i+1] = count & 255;
        reads[4*i+2] = count >> 8; reads[4*i+3] = p2;
    }
}
static void now(int want, unsigned int p1, unsigned int count, unsigned int p2, unsigned int n)
{
    u32 lo = 0x1234, hi = 0x5678;
    unsigned int before = enabled;
    samples(p1, count, p2, n);
    check(sys_time_now(&lo, &hi) == want);
    check(read_at == read_n && enabled == before);
    if (want) check(lo == 0x1234 && hi == 0x5678);
    event('L', (unsigned int)lo, (unsigned int)hi);
}
void _start(void)
{
    unsigned int state, count, p1, p2;
    struct pit_setup prior;
    check(sysclk_hz() == SYSCLK_1997 && !sysclk_is_8mhz() && !sysclk_detected());
    check(!pit_get_setup()->valid);
    now(OS32_ERR_NOSYS, 0, 0, 0, 0);
    lowmem[BIOS_WORK_SYSCLK] = TEST_CLOCK_BYTE;
    sysclk_detect();
    check(sysclk_hz() == ((TEST_CLOCK_BYTE & BIOS_SYSCLK_8MHZ) ? SYSCLK_1997 : SYSCLK_2458));
    check(sysclk_is_8mhz() == !!(TEST_CLOCK_BYTE & BIOS_SYSCLK_8MHZ));
    lowmem[BIOS_WORK_SYSCLK] ^= BIOS_SYSCLK_8MHZ;
    sysclk_detect(); check(clocks == 1 && sysclk_detected());
    check(pit_init(PIT_HZ) == 0);
    check(pit_get_setup()->period_us == 10000);
#ifdef TEST_RETRY_VALUE
    /* Separate run keeps the captured L13 trace unchanged. Use hardware I/O
     * samples, not the feed: reject count=1, then accept the half-period.
     * 429497 ticks + 5000us = hi:lo 1:7704 (the next tick is 1:17704). */
    for (state = 0; state < 2; state++) {
        u32 lo = 0x1234, hi = 0x5678;
        enabled = state;
        tick_value = 429497 + state;
        samples(0, pit_get_setup()->reload / 2, 0, 2);
        reads[1] = 1; reads[2] = 0; reads[3] = 1;
        check(sys_time_now(&lo, &hi) == 0);
        check(read_at == read_n && enabled == state);
        check(lo == (state ? 17704U : 7704U) && hi == 1);
        /* Same timestamp is not a clamp; a later reload at the same tick is.
         * Keep the 64-bit result, exact count delta and caller's IF state. */
        u32 clamps = ktime_clamp_count;
        samples(0, pit_get_setup()->reload / 2, 0, 1);
        check(sys_time_now(&lo, &hi) == 0 && read_at == read_n && enabled == state);
        check(lo == (state ? 17704U : 7704U) && hi == 1);
        check(ktime_clamp_count == clamps);
        samples(0, pit_get_setup()->reload, 0, 1);
        check(sys_time_now(&lo, &hi) == 0 && read_at == read_n && enabled == state);
        check(lo == (state ? 17704U : 7704U) && hi == 1);
        check(ktime_clamp_count == clamps + 1);
    }
    finish(0);
#endif
    check(pit_init(0) == PIT_ERR_HZ);
    prior = *pit_get_setup();
    /* The real sysclk getter never returns an unknown clock. Change its stored
     * value only inside this test translation unit to exercise both failures. */
    s_hz = 0;
    check(pit_init(PIT_HZ) == PIT_ERR_CLOCK);
    check(pit_get_setup()->reload == prior.reload && pit_get_setup()->valid == prior.valid);
    s_hz = prior.clk_hz;
    s_pit.mode = 0;
    now(OS32_ERR_NOSYS, 0, 0, 0, 0);
    s_pit = prior;
    s_pit.reload = 0;
    now(OS32_ERR_NOSYS, 0, 0, 0, 0);
    s_pit = prior;
    for (state = 0; state < 2; state++) {
        enabled = state;
        for (p1 = 0; p1 < 2; p1++) for (p2 = 0; p2 < 2; p2++) {
            for (count = 0; count < 4; count++) {
                unsigned int c = count == 0 ? 0 : count == 1 ? 1 : prior.reload + count - 2;
                tick_value = 100 + count;
                now(!p1 && p2 ? OS32_ERR_AGAIN : 0, p1, c, p2,
                    !p1 && p2 ? TIME_RETRY_MAX : 1);
            }
        }
        /* First boundary retries; the second stable sample succeeds. */
        samples(0, prior.reload, 0, 2);
        reads[3] = 1;
        check(sys_time_now(0, 0) == 0 && read_at == read_n && enabled == state);
        /* A backwards hardware sample must still pass through the clamp. */
        tick_value = 1;
        now(0, 0, prior.reload, 0, 1);
        samples(0, 1, 0, 1);
        check(sys_time_now(0, 0) == 0 && read_at == read_n && enabled == state);
    }
    check(ktime_clamp_count > 0);
    finish(0);
}
