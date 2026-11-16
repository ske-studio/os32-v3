/* Fake raw clock for fixtures that execute the real Tier budget code. */
#ifndef TEST_RX_IO_CLOCK_H
#define TEST_RX_IO_CLOCK_H
#include "kernel/io_clock.h"
#include "clock_source.h"
#ifndef TEST_RX_CLOCK_LOCK
#define TEST_RX_CLOCK_LOCK() 0u
#define TEST_RX_CLOCK_UNLOCK(f) ((void)(f))
#endif
static int test_rx_clock_read(u32 *count, u32 *source, u32 *generation)
{
    u32 hi;
    unsigned int flags = TEST_RX_CLOCK_LOCK();
    int rc = platform_clock_read_us(count, &hi);
    TEST_RX_CLOCK_UNLOCK(flags);
    *source = *generation = 1;
    return rc;
}
static int test_rx_clock_count(u32 us, u32 source, u32 generation, u32 *count)
{
    (void)source; (void)generation; *count = us; return 0;
}
static void test_rx_clock_install(void)
{
    static const struct io_clock_ops ops = { test_rx_clock_read, test_rx_clock_count, platform_clock_read_us };
    io_clock_set_ops(&ops);
}
#endif
