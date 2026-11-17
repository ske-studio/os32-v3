/* Freestanding T3 fixture: actual driver + unchanged product math. */
#include "drivers/sound/cs4231/pcm_cs4231_math.c"
#include "drivers/sound/cs4231/pcm_cs4231.c"

#define REQUIRE(x) do { if (!(x)) return 1; } while (0)
static u8 ring[PCM_RING_BYTES], staging[PCM_STG_BYTES];
static u32 clock_us, position, second_position, reads, fail_read;
static u32 copies, clears, accesses;
static int inject_tick, injected_bad;
static u8 index_reg, pi;
int pcm_shim_irq_depth;
volatile u32 tick_count;
void pcm_shim_if_on(void) { }
unsigned int pcm_shim_inp(unsigned int port)
{
    accesses++;
    return port == PCM_PORT_R1 && index_reg == PCM_I_ALTSTAT ? pi : 0;
}
void pcm_shim_outp(unsigned int port, unsigned int value)
{
    accesses++;
    if (port == PCM_PORT_R0) index_reg = (u8)value;
    if (port == PCM_PORT_R1 && index_reg == PCM_I_ALTSTAT) pi = (u8)value;
}
int sys_time_now(u32 *lo, u32 *hi) { *lo = clock_us; *hi = 0; return 0; }
void tick_clock_jiffy64(u32 *lo, u32 *hi) { *lo = tick_count; *hi = 0; }
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = dst;
    const u8 *s = src;
    copies++;
    if (inject_tick) {
        u32 staged = g_pcm.stg.staged, r = g_pcm.stg.r;
        inject_tick = 0;
        if (pcm_shim_irq_depth != 0) injected_bad = 1;
        pcm_shim_irq_depth = 1;
        pcm_tick();
        pcm_shim_irq_depth = 0;
        if (g_pcm.stg.staged != staged || g_pcm.stg.r != r ||
            g_pcm.half != 0 || ring[0] != 0x55) injected_bad = 1;
    }
    while (n--) *d++ = *s++;
    return dst;
}
void *kmemset(void *dst, int val, u32 n)
{
    u8 *d = dst;
    clears++;
    while (n--) *d++ = (u8)val;
    return dst;
}
void dma_chan_mask(unsigned int ch) { (void)ch; }
void dma_chan_unmask(unsigned int ch) { (void)ch; }
void dma_chan_ack_tc(unsigned int ch) { (void)ch; }
int dma_chan_setup(unsigned int ch, u32 p, u32 n, int d, int m)
{ (void)ch; (void)p; (void)n; (void)d; (void)m; return 0; }
int dma_chan_remaining(unsigned int ch, u32 *left, int *tc)
{
    u32 pos = ++reads == 1 ? position : second_position;
    (void)ch;
    if (reads == fail_read) return OS32_ERR_AGAIN;
    *left = PCM_RING_BYTES - pos * PCM_FRAME_BYTES;
    *tc = 0;
    return 0;
}
int res_owner_get(void) { return 1; }

/* The linker removes unrelated foreground allocation/registration paths. */
static void setup(u32 rate, u8 state, u32 half)
{
    kmemset(&g_pcm, 0, sizeof(g_pcm));
    kmemset(pcm_diag_observe, 0, sizeof(pcm_diag_observe));
    kmemset(pcm_diag_refill, 0, sizeof(pcm_diag_refill));
    kmemset(ring, 0x55, sizeof(ring));
    kmemset(staging, 0x33, sizeof(staging));
    g_pcm.rate = rate;
    g_pcm.state = state;
    g_pcm.half = (u8)half;
    g_pcm.pos = (half + 1U) * PCM_HALF_FRAMES - 1U;
    g_pcm.filled[0] = g_pcm.filled[1] = PCM_HALF_FRAMES;
    g_pcm.stg.staged = PCM_STG_FRAMES;
    g_pcm.last_data_half = (u8)half;
    g_pcm.drain = PCM_DRAIN_IN;
    s_ring = ring;
    s_stg = staging;
    s_claimed = s_mce_busy = 0;
    s_last_us = 0;
    clock_us = reads = fail_read = copies = clears = accesses = 0;
    pcm_shim_irq_depth = 1;
    pi = PCM_ALT_PI;
}
static void at_distance(u32 half, u32 distance)
{
    position = second_position = (half + 1U) * PCM_HALF_FRAMES - distance;
    reads = 0;
}
static int untouched(const struct pcm_core *before)
{
    const u8 *a = (const u8 *)before, *b = (const u8 *)&g_pcm;
    u32 i;
    for (i = 0; i < sizeof(g_pcm); i++) if (a[i] != b[i]) return 0;
    return copies == 0 && clears == 0;
}
static int healthy(u8 state)
{
    return g_pcm.state == state && !g_pcm.drain_failed &&
           !g_pcm.underruns && !g_pcm.repeats && !g_pcm.resyncs;
}
static int verify(u32 rate, u8 state, u32 old_half)
{
    /* Independent design reference: never derive the simulated delay from
     * the helper under test. Both directions of ring wrap are exercised. */
    u32 guard = rate == PCM_RATE_44100 ? 489U : 245U;
    u32 threshold = PCM_REFILL_MARGIN + guard;
    u32 new_half = old_half ^ 1U;
    struct pcm_core before;


    setup(rate, state, old_half);
    at_distance(new_half, threshold + 1U);
    clock_us = (PCM_HALF_FRAMES - threshold) * USEC_PER_SEC / rate;
    before = g_pcm;
    pcm_tick();
    REQUIRE(untouched(&before) && pi == 0 && reads == 2);
    REQUIRE(pcm_diag_observe[PCM_FROM_TICK].calls == 1);
    REQUIRE(pcm_diag_refill[PCM_FROM_TICK].calls == 0);
    at_distance(new_half, threshold + 1U - guard);
    clock_us += (guard * USEC_PER_SEC + rate - 1U) / rate;
    pcm_tick();
    REQUIRE(healthy(state) && copies == 1 && clears == 0);
    REQUIRE(g_pcm.stg.staged == PCM_HALF_FRAMES);
    REQUIRE(g_pcm.filled[old_half] == PCM_HALF_FRAMES);
    REQUIRE(ring[old_half * PCM_HALF_BYTES] == 0x33);
    REQUIRE(ring[new_half * PCM_HALF_BYTES] == 0x55);
    REQUIRE(pcm_diag_refill[PCM_FROM_TICK].calls == 1);
    REQUIRE(pcm_diag_refill[PCM_FROM_IRQ10].calls == 0);
    reads = 0;
    (void)pcm_irq(PCM_IRQ, 0);
    REQUIRE(copies == 1 && g_pcm.stg.staged == PCM_HALF_FRAMES);

    /* IRQ10 wins while a tick is postponing: one consume, no tick copy. */
    setup(rate, state, old_half);
    at_distance(new_half, threshold + 1U);
    pcm_tick();
    reads = 0;
    REQUIRE(pcm_irq(PCM_IRQ, 0) == IRQ_NONE); /* tick already acked PI */
    REQUIRE(copies == 1 && healthy(state));
    REQUIRE(pcm_diag_refill[PCM_FROM_IRQ10].calls == 1);
    reads = 0;
    pcm_tick();
    REQUIRE(copies == 1 && pcm_diag_refill[PCM_FROM_TICK].calls == 0);

    /* F is inclusive. The worst phase under the old S=1001 assumption
     * (605/559 remaining) must already be rescued, before another G frames. */
    for (u32 i = 0; i < 3; i++) {
        u32 d = i == 1 ? threshold : i == 0 ?
                (rate == PCM_RATE_44100 ? 605U : 559U) : PCM_REFILL_MARGIN;
        setup(rate, state, old_half);
        at_distance(new_half, d);
        pcm_tick();
        REQUIRE(copies == 1 && healthy(state));
    }
    setup(rate, state, old_half);
    at_distance(new_half, PCM_REFILL_MARGIN - 1U);
    pcm_tick();
    REQUIRE(!copies && g_pcm.repeats == 1 &&
            g_pcm.state == (state == PCM_ST_RUNNING ? PCM_ST_RS_STOP : PCM_ST_STOP_REQ));

    /* Recheck can cross the threshold, wrap to the old half (lost), or fail.
     * None may consume the first, stale sample. */
    setup(rate, state, old_half);
    at_distance(new_half, threshold + 1U);
    second_position++;
    pcm_tick();
    REQUIRE(copies == 1 && healthy(state) && reads == 2);
    setup(rate, state, old_half);
    at_distance(new_half, threshold);
    fail_read = 2;
    pcm_tick();
    REQUIRE(!copies && g_pcm.half == old_half && g_pcm.stg.staged == PCM_STG_FRAMES);
    setup(rate, state, old_half);
    at_distance(new_half, threshold);
    second_position = old_half * PCM_HALF_FRAMES;
    pcm_tick();
    REQUIRE(!copies && g_pcm.repeats == 1);

    /* An excessive observation gap must not defer the existing observer. */
    setup(rate, state, old_half);
    at_distance(new_half, threshold + 1U);
    clock_us = PCM_HALF_FRAMES * USEC_PER_SEC / rate;
    pcm_tick();
    REQUIRE(copies == 1 && healthy(state));

    /* Zero-fill still belongs to the selected origin. Normal drain completion
     * keeps STOP_REQ, rather than being stranded by repeated deferrals. */
    setup(rate, PCM_ST_DRAINING, old_half);
    g_pcm.stg.staged = 0;
    g_pcm.drain = PCM_DRAIN_SIL;
    at_distance(new_half, threshold);
    pcm_tick();
    REQUIRE(!copies && clears == 1 && g_pcm.state == PCM_ST_STOP_REQ);
    REQUIRE(pcm_diag_refill[PCM_FROM_TICK].calls == 1);
    REQUIRE(pcm_refill_guard_frames(rate) == guard);
    REQUIRE(pcm_refill_threshold(rate) == threshold);
    REQUIRE(pcm_refill_threshold_us(rate) ==
            (rate == PCM_RATE_44100 ? 22699U : 34332U));
    REQUIRE(threshold < PCM_HALF_FRAMES);
    return 0;
}
static int write_boundary(void)
{
    static const u8 src[PCM_FRAME_BYTES] = {1, 2, 3, 4};
    setup(PCM_RATE_44100, PCM_ST_RUNNING, 0);
    s_owner = 1;
    g_pcm.stg.staged = g_pcm.stg.w = PCM_HALF_FRAMES;
    at_distance(1, 1002U);
    pcm_shim_irq_depth = 0;
    inject_tick = 1;
    REQUIRE(pcm_write(src, sizeof(src)) == sizeof(src));
    REQUIRE(!inject_tick && !injected_bad && pcm_shim_irq_depth == 0);
    REQUIRE(g_pcm.stg.staged == PCM_HALF_FRAMES + 1U && g_pcm.stg.r == 0);
    REQUIRE(g_pcm.stg.w == PCM_HALF_FRAMES + 1U && copies == 1);
    REQUIRE(staging[PCM_HALF_BYTES] == src[0]);
    at_distance(1, 1001U);
    pcm_shim_irq_depth = 1;
    pcm_tick();
    REQUIRE(copies == 2 && g_pcm.stg.staged == 1 && healthy(PCM_ST_RUNNING));

    /* close_pending must be honoured even on a deferred transition. */
    setup(PCM_RATE_44100, PCM_ST_RUNNING, 0);
    g_pcm.close_pending = 1;
    at_distance(1, 1002U);
    pcm_tick();
    REQUIRE(healthy(PCM_ST_DRAINING) && !copies && !clears);
    REQUIRE(g_pcm.stg.staged == PCM_STG_FRAMES && g_pcm.half == 0);
    return 0;
}
static int run(void)
{
    for (u32 rate = PCM_RATE_22050; rate <= PCM_RATE_44100; rate *= 2U)
        for (u32 state = PCM_ST_RUNNING; state <= PCM_ST_DRAINING; state++)
            for (u32 half = 0; half < 2; half++)
                if (verify(rate, (u8)state, half)) return 1;
    return write_boundary();
}
void _start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
