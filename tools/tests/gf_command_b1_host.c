/* Link with the real hwdev/gf/push.c and A2's VA read observer. This
 * fixture supplies gf_io callbacks and the B7 gf2_fault_check stub locally. */
#include "hwdev/gf/command.h"

/* A2 contract: watch actual CPU reads of this one volatile u32 (not writes)
 * without changing push.c. begin returns 0 on success and resets the count;
 * count does not access the watched word; end restores its ordinary mapping.
 * No no-op fallback: missing observation must fail to link, not pass silently. */
extern int gf2_host_va_read_begin(volatile u32 *word);
extern u32 gf2_host_va_read_count(void);
extern void gf2_host_va_read_end(void);

#define BASE 0x100000U
#define USER (GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE)
#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)
static u32 ring[GF2_PUSH_WORDS];
static struct gf2_chan ch;
static struct gf_foundation f;
static struct fake {
    u32 get[8], put[8], readbacks_at_put[8], now, waits;
    unsigned gets, reads, writes, fault_after_reads;
    bool fault, read_fail, write_fail, clock_fail, wait_fail, watch_readback;
} m;

static int read_reg(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    struct fake *p = owner;
    if (bar != GF_MODE_REGISTER_BAR || off != USER + GF2_USER_GET ||
        width != sizeof(u32) || p->read_fail) return -1;
    *out = p->get[p->reads < p->gets ? p->reads : p->gets - 1U];
    ++p->reads;
    return 0;
}
static int write_reg(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    struct fake *p = owner;
    if (bar != GF_MODE_REGISTER_BAR || off != USER + GF2_USER_PUT ||
        width != sizeof(u32) || p->writes == 8 || p->write_fail) return -1;
    if (p->watch_readback)
        p->readbacks_at_put[p->writes] = gf2_host_va_read_count();
    p->put[p->writes++] = value;
    return 0;
}
static int clock_us(void *owner, u32 *now)
{
    struct fake *p = owner;
    *now = p->now;
    return p->clock_fail ? -1 : 0;
}
static int wait_us(void *owner, u32 us)
{
    struct fake *p = owner;
    if (us < GF2_WAIT_MIN_US || us > GF2_WAIT_MAX_US || p->wait_fail) return -1;
    p->now += us;
    ++p->waits;
    return 0;
}
int gf2_fault_check(struct gf2_chan *c)
{
    if (m.fault || (m.fault_after_reads && m.reads >= m.fault_after_reads))
        c->state = GF2_FAULT;
    return c->state == GF2_FAULT ? GF2_EFAULT : GF2_OK;
}
static void fresh(enum gf2_state state)
{
    m = (struct fake){ .gets = 1 };
    f = (struct gf_foundation){ .io = { .opaque = &m, .read = read_reg,
        .write = write_reg, .timing = { .clock_us = clock_us, .wait_us = wait_us } } };
    ch = (struct gf2_chan){ .f = &f, .state = state, .B = BASE, .va = ring,
        .cur = GF2_PUSH_SKIPS, .max = GF2_PUSH_MAX,
        .free = GF2_PUSH_MAX - GF2_PUSH_SKIPS };
    for (u32 i = 0; i < GF2_PUSH_WORDS; ++i) ring[i] = 0;
}
static int test_readback_order(void)
{
    fresh(GF2_RUN);
    REQUIRE(!gf2_push_reference(&ch, 1, 100));
    REQUIRE(!gf2_host_va_read_begin(ch.va));
    m.watch_readback = true;
    REQUIRE(gf2_host_va_read_count() == 0);
    REQUIRE(!gf2_kick(&ch));
    REQUIRE(m.writes == 1 && m.readbacks_at_put[0] == 1);
    REQUIRE(gf2_host_va_read_count() == 1);
    REQUIRE(!gf2_kick(&ch)); /* No pending data: no readback or PUT. */
    REQUIRE(m.writes == 1 && gf2_host_va_read_count() == 1);
    gf2_host_va_read_end();

    fresh(GF2_RUN);
    ch.cur = ch.max - 1; ch.put = ch.cur - 2; ch.free = 1;
    m.get[0] = BASE + ch.cur * 4;
    REQUIRE(!gf2_host_va_read_begin(ch.va));
    m.watch_readback = true;
    REQUIRE(!gf2_push_reference(&ch, 2, 100));
    /* Pending tail and wrap PUT must each have a preceding readback. */
    REQUIRE(m.writes == 2 && m.readbacks_at_put[0] == 1 &&
            m.readbacks_at_put[1] == 2);
    REQUIRE(gf2_host_va_read_count() == 2);
    REQUIRE(!gf2_kick(&ch));
    REQUIRE(m.writes == 3 && m.readbacks_at_put[2] == 3);
    REQUIRE(gf2_host_va_read_count() == 3);
    gf2_host_va_read_end();
    return 0;
}
static int test(void)
{
    u32 data[2] = {0x12345678U, 0xabcdef01U};
    fresh(GF2_INIT);
    REQUIRE(gf2_emit(&ch, 0, 0x50, false, data, 1, 100) == GF2_ESTATE);
    REQUIRE(!gf2_push_reference(&ch, data[0], 100)); /* GET=0, initial free. */
    REQUIRE(ch.cur == 34 && ch.free == GF2_PUSH_MAX - 34 && !m.reads);
    REQUIRE(ring[32] == 0x00040050U && ring[33] == data[0]);
    REQUIRE(!gf2_kick(&ch) && ch.put == 34 && m.put[0] == BASE + 136);
    REQUIRE(!gf2_kick(&ch) && m.writes == 1);
    ch.state = GF2_DRAIN;
    REQUIRE(gf2_emit(&ch, 0, 0, false, data, 1, 100) == GF2_ESTATE);
    REQUIRE(!gf2_push_reference(&ch, 2, 100) && !gf2_kick(&ch));
    ch.state = GF2_OFF;
    REQUIRE(gf2_push_reference(&ch, 3, 100) == GF2_ESTATE);
    REQUIRE(gf2_kick(&ch) == GF2_ESTATE);
    fresh(GF2_RUN);
    REQUIRE(gf2_emit(&ch, 8, 0, false, data, 1, 100) == GF2_EINVAL);
    REQUIRE(gf2_emit(&ch, 0, 0x51, false, data, 1, 100) == GF2_EINVAL);
    REQUIRE(gf2_emit(&ch, 0, 0x2000, false, data, 1, 100) == GF2_EINVAL);
    REQUIRE(gf2_emit(&ch, 0, 0, false, data, 0, 100) == GF2_EINVAL);
    REQUIRE(gf2_emit(&ch, 0, 0, false, data, 2048, 100) == GF2_EINVAL);
    REQUIRE(gf2_emit(&ch, 0, 0x1ffc, false, data, 2, 100) == GF2_EINVAL);
    REQUIRE(ch.cur == 32 && ch.free == GF2_PUSH_MAX - 32 && !m.writes);
    REQUIRE(!gf2_emit(&ch, 7, 0x1ffc, true, data, 2, 100));
    REQUIRE(ring[32] == 0x4008fffcU && ring[33] == data[0] && ring[34] == data[1]);
    REQUIRE(!gf2_emit(&ch, 0, 0, false, data, 1, 100)); /* Object bind. */
    REQUIRE(ring[35] == 0x40000U && ring[36] == data[0]);
    fresh(GF2_RUN);
    ch.put = ch.cur; ch.free = 0;
    m.get[0] = BASE + 34 * 4; m.get[1] = BASE + 35 * 4; m.gets = 2;
    REQUIRE(!gf2_push_reference(&ch, 7, 100)); /* Leave the word before GET. */
    REQUIRE(m.reads == 2 && m.waits == 1 && ch.free == 0 && ch.cur == 34);
    fresh(GF2_RUN);
    ch.cur = ch.put = ch.max - 1; ch.free = 1;
    m.get[0] = BASE - 4; m.get[1] = BASE + 1;
    m.get[2] = BASE + (GF2_PUSH_MAX + 1U) * 4;
    m.get[3] = BASE + 31 * 4; m.get[4] = BASE + ch.cur * 4;
    m.get[5] = BASE + 32 * 4; m.get[6] = BASE + 40 * 4; m.gets = 7;
    REQUIRE(!gf2_push_reference(&ch, 8, 100)); /* get=cur; wrap waits past 32. */
    REQUIRE(ring[GF2_PUSH_MAX - 1] == (BASE | GF2_PUSH_JUMP));
    REQUIRE(m.reads == 7 && m.writes == 1 && m.put[0] == BASE + 128);
    REQUIRE(ch.cur == 34 && ch.put == 32 && ch.free == 5 && ring[33] == 8);
    for (u32 i = 0; i < GF2_PUSH_SKIPS; ++i) REQUIRE(!ring[i]);
    REQUIRE(!gf2_kick(&ch) && m.put[1] == BASE + 136);
    fresh(GF2_RUN);
    ch.cur = ch.max - 1; ch.put = ch.cur - 2; ch.free = 1;
    m.get[0] = BASE + ch.cur * 4;
    REQUIRE(!gf2_push_reference(&ch, 9, 100));
    REQUIRE(m.writes == 2 && m.put[0] == BASE + (GF2_PUSH_MAX - 1) * 4 &&
            m.put[1] == BASE + 128); /* Pending tail precedes wrap PUT. */
    fresh(GF2_RUN);
    ch.put = ch.cur; ch.free = 0; m.get[0] = BASE + 128;
    REQUIRE(!gf2_push_reference(&ch, 10, 100) && m.reads == 1); /* get=32. */
    fresh(GF2_RUN);
    ch.put = ch.cur; ch.free = 0; m.now = 0xfffffffeU;
    REQUIRE(gf2_push_reference(&ch, 11, 1) == GF2_ETIME && m.waits == 3);
    REQUIRE(ch.cur == 32 && !m.writes); /* Invalid GET never became space. */
    fresh(GF2_RUN); m.clock_fail = true;
    REQUIRE(gf2_push_reference(&ch, 12, 100) == GF2_EIO && ch.cur == 32);
    fresh(GF2_RUN); ch.put = ch.cur; ch.free = 0; m.read_fail = true;
    REQUIRE(gf2_push_reference(&ch, 12, 100) == GF2_EIO);
    fresh(GF2_RUN); ch.put = ch.cur; ch.free = 0; m.wait_fail = true;
    REQUIRE(gf2_push_reference(&ch, 12, 100) == GF2_EIO);
    fresh(GF2_RUN); m.write_fail = true;
    REQUIRE(gf2_kick(&ch) == GF2_EIO && ch.put == 0);
    fresh(GF2_RUN); m.fault = true;
    REQUIRE(gf2_kick(&ch) == GF2_EFAULT && !m.writes);
    REQUIRE(gf2_push_reference(&ch, 13, 100) == GF2_EFAULT);
    fresh(GF2_RUN); ch.put = ch.cur; ch.free = 0; m.fault_after_reads = 1;
    REQUIRE(gf2_push_reference(&ch, 14, 100) == GF2_EFAULT && !m.writes);
    return 0;
}
void _start(void)
{
    int rc = test_readback_order();
    if (!rc) rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
