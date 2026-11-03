/* Execute the real player with rational scan timing and injected IRQ latency. */
#include "types.h"
#include "pc98.h"
#include "pegc.h"
#define IO_H

static unsigned long long clock_us;
static u32 den, total, high_lines, raw_mode, scale = 1000;
static int irq_on = 1, frozen, missing_v, missing_h, clock_error, backwards;
static u32 calls, writes, mode_reads, protected_writes, open_writes;
static unsigned long long closed_at, opened_at, max_closed, min_open;
static unsigned long long inject_at, inject_us;
static int track_irq;
#define HOST_WRITES 4096u
static unsigned long long write_time[HOST_WRITES];
static u32 write_port[HOST_WRITES], write_value[HOST_WRITES];

static u32 failed_line;
static void finish(int code)
{
    if (code) {
        char text[] = "clock=00000000 writes=00000000 closed=00000000 line=00000000\n";
        u32 values[] = {(u32)clock_us, writes, (u32)max_closed, failed_line};
        const int offsets[] = {6, 22, 38, 52};
        for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++)
            text[offsets[i]+j] = "0123456789abcdef"[(values[i] >> (28 - 4*j)) & 15];
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(text), "d"(sizeof(text)-1) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(c) do { if (!(c)) { failed_line = __LINE__; finish(__LINE__ % 250 + 1); } } while (0)

/* The image fixture has its own scan oscillator. Time is in 1/100 us;
 * costs and faults are inputs, never inferred from a player's edge/row.
 * The old logo fixtures below retain their original clock and assertions. */
#define SCAN_DEN 100u
struct HostScanInput {
    u32 h, blank, in_cost, branch_cost, out_cost, clock_cost, cli_clock_cost;
    unsigned long long irq_at, irq_cost, delay_at, delay_cost, lost_h, early_h;
};
static struct HostScan {
    int enabled, sampling, clock_seen, settle_error, sample_error;
    struct HostScanInput input;
    unsigned long long time, next_visible, first_visible, last_visible;
    unsigned long long irq_at, irq_count, sample_at, scan_reads, scan_clocks;
    u32 row, rows, index, samples, settles;
    u8 palette[16][3], visible[400][16][3];
    unsigned long long changed[16][3], effective[400][16][3];
    unsigned long long out_at[HOST_WRITES];
    u8 out_if[HOST_WRITES];
} scan;
static void scan_step(unsigned long long delta);
static unsigned int scan_status(void);
static void scan_out(unsigned int port, unsigned int value);

static void advance(void)
{
    if (++calls > 4000000) finish(251); /* timeout mutants must die at runtime */
    if (!frozen) clock_us++;
    if (irq_on && inject_us && clock_us >= inject_at) {
        clock_us += inject_us;
        inject_us = 0;
    }
}

int sys_time_now(u32 *lo, u32 *hi)
{
    if (scan.enabled) {
        if (!irq_on) {
            if (scan.clock_seen) scan.scan_clocks++;
            scan.clock_seen = 1;
        }
        scan_step(irq_on ? scan.input.clock_cost : scan.input.cli_clock_cost);
        if (clock_error) return -1;
        unsigned long long t = frozen ? 0 : scan.time / SCAN_DEN;
        if (backwards) t = 10000000 - t;
        *lo = t; *hi = t >> BITS_PER_U32;
        return 0;
    }
    advance();
    if (clock_error) return -1;
    unsigned long long t = backwards ? 10000000 - clock_us : clock_us;
    *lo = t;
    *hi = t >> BITS_PER_U32;
    return 0;
}

static unsigned int inp(unsigned int port)
{
    if (port == PEGC_HSYNC_PORT) { mode_reads++; return raw_mode; }
    REQUIRE(port == GDC_GFX_STAT);
    if (scan.enabled) return scan_status();
    unsigned long long phase = clock_us * den * 1000 / scale;
    phase %= (unsigned long long)total * 100000;
    u32 status = 0;
    if (!missing_v && phase < (unsigned long long)high_lines * 100000)
        status |= GDC_STAT_VSYNC;
    /* An 8us HBLANK at the end of each horizontal line. */
    if (!missing_h && phase % 100000 >= 100000 - 8 * den)
        status |= GDC_STAT_HBLANK;
    return status;
}

static int _irq_enabled(void) { return irq_on; }
static unsigned int irq_save(void)
{
    unsigned int saved = irq_on;
    REQUIRE(irq_on);
    if (track_irq && opened_at) {
        unsigned long long gap = clock_us - opened_at;
        if (gap < min_open) min_open = gap;
    }
    irq_on = 0;
    closed_at = scan.enabled ? scan.time : clock_us;
    if (scan.enabled) scan.clock_seen = 0;
    return saved;
}
static void irq_restore(unsigned int saved)
{
    REQUIRE(!irq_on);
    unsigned long long duration = (scan.enabled ? scan.time : clock_us) - closed_at;
    if (duration > max_closed) max_closed = duration;
    irq_on = saved;
    opened_at = clock_us;
}
static void outp(unsigned int port, unsigned int value)
{
    if (scan.enabled) { scan_out(port, value); return; }
    REQUIRE(writes < HOST_WRITES);
    write_time[writes] = clock_us;
    write_port[writes] = port;
    write_value[writes++] = value;
    if (irq_on) open_writes++; else protected_writes++;
    clock_us++; /* nonzero OUT cost, including four-OUT critical sections */
}

#include "../../drivers/video/pc98/splash_raster.c"
#define finish pl_host_finish
#define PL_PROFILES_CONST
#define now_us pl_host_now_us
#include "../../drivers/video/pc98/splash_raster_image.c"
#undef now_us
#undef PL_PROFILES_CONST
#undef finish

static void setup(int mode, int vs_only)
{
    scan.enabled = 0;
    clock_us = 0; den = mode ? 3147 : 2483; total = mode ? 449 : 440;
    high_lines = vs_only ? (mode ? 2 : 8) : total - 400;
    raw_mode = mode ? 0xFF : 0xFE; /* D1 and high bits are deliberately set. */
    scale = 1000; irq_on = 1;
    frozen = missing_v = missing_h = clock_error = backwards = 0;
    calls = writes = mode_reads = protected_writes = open_writes = 0;
    closed_at = opened_at = max_closed = inject_at = inject_us = 0;
    min_open = 1000000; track_irq = 0;
    pc98_splash_raster_reset();
}

static GFX_RasterPalTable logo;
static struct OsriImage fixed;
static void make_logo(void)
{
    logo.count = 100;
    for (int i = 0; i < 100; i++) {
        logo.entries[i] = (GFX_RasterPalEntry){ .line = i * 4,
            .pal_idx = 1, .r = i & 15, .g = (i + 3) & 15, .b = (i + 7) & 15 };
    }
}

static void ports(void)
{
    for (u32 i = 0; i < writes; i += 4) {
        REQUIRE(write_port[i] == PAL_IDX_PORT);
        REQUIRE(write_port[i+1] == PAL_G_PORT);
        REQUIRE(write_port[i+2] == PAL_R_PORT);
        REQUIRE(write_port[i+3] == PAL_B_PORT);
    }
}

static void qualification(void)
{
    /* IF=0 defers the first measurement; the next IF=1 access retries. */
    setup(0, 0); irq_on = 0;
    struct SplashRaster *deferred = pc98_splash_raster_state();
    REQUIRE(deferred->last_result == SPLASH_RASTER_NO_SYNC);
    REQUIRE(!deferred->checked && !deferred->dynamic_ok && !irq_on);
    REQUIRE(!mode_reads && !calls && !writes);
    irq_on = 1;
    REQUIRE(pc98_splash_raster_state() == deferred);
    REQUIRE(deferred->checked && deferred->dynamic_ok);
    REQUIRE(deferred->last_result == SPLASH_RASTER_OK && mode_reads == 1 && irq_on);

    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++) {
        setup(mode, vs);
        struct SplashRaster *s = pc98_splash_raster_state();
        REQUIRE(s->dynamic_ok && s->mode == (u32)mode);
        REQUIRE(s->total_lines == total && s->line_den == den);
        REQUIRE(s->active0_lines == (u32)(vs ? (mode ? 34 : 25) : 0));
        REQUIRE(clock_us >= (unsigned long long)8 * total * 100000 / den);
        u32 read_calls = calls;
        REQUIRE(pc98_splash_raster_state() == s && calls == read_calls);
        REQUIRE(mode_reads == 1);
        raw_mode ^= 1;
        REQUIRE(pc98_splash_raster_state()->mode == (u32)mode);
        pc98_splash_raster_reset();
        REQUIRE(!s->checked && !s->dynamic_ok);
        setup(mode, vs); scale = 1009;
        REQUIRE(pc98_splash_raster_state()->dynamic_ok);
        setup(mode, vs); scale = 1011;
        REQUIRE(!pc98_splash_raster_state()->dynamic_ok);
        setup(mode, vs); scale = 989;
        REQUIRE(!pc98_splash_raster_state()->dynamic_ok);
        setup(mode, vs); high_lines = 15;
        REQUIRE(!pc98_splash_raster_state()->dynamic_ok);
    }
}

static void replay(void)
{
    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++) {
        setup(mode, vs); make_logo();
        struct SplashRaster *s = pc98_splash_raster_state();
        track_irq = 1;
        REQUIRE(!pc98_splash_raster_table(s, &logo));
        REQUIRE(writes == 400 && protected_writes == 396 && open_writes == 4);
        REQUIRE(s->frame == 1 && !s->dropped && !s->late && !s->unapplied);
        REQUIRE(irq_on && max_closed * den <= 2 * 100000);
        REQUIRE(min_open * den >= 100000);
        ports();
        for (u32 i = 0; i < 100; i++) {
            REQUIRE(write_value[4*i+1] == logo.entries[i].g);
            REQUIRE(write_value[4*i+2] == logo.entries[i].r);
            REQUIRE(write_value[4*i+3] == logo.entries[i].b);
            if (i) {
                unsigned long long phase = write_time[4*i] * den % ((unsigned long long)total * 100000);
                unsigned long long target = (unsigned long long)(high_lines + s->active0_lines + 4*i) * 100000;
                REQUIRE(phase < target && target - phase <= 10 * den);
            }
        }
        writes = protected_writes = open_writes = 0;
        logo.count = 3; logo.entries[1].line = 10; logo.entries[2].line = 398;
        REQUIRE(!pc98_splash_raster_table(s, &logo) && writes == 12);
        logo.entries[2].line = 11;
        REQUIRE(pc98_splash_raster_table(s, &logo) == SPLASH_RASTER_NO_SYNC);
        logo.entries[2].line = 10;
        REQUIRE(pc98_splash_raster_table(s, &logo) == SPLASH_RASTER_NO_SYNC);
        logo.entries[2].line = 400;
        REQUIRE(pc98_splash_raster_table(s, &logo) == SPLASH_RASTER_NO_SYNC);
        irq_on = 0; make_logo(); writes = 0;
        REQUIRE(pc98_splash_raster_table(s, &logo) == SPLASH_RASTER_NO_SYNC);
        REQUIRE(!irq_on && !writes);
    }
}

static void faults(void)
{
    setup(0, 0); missing_v = 1;
    REQUIRE(pc98_splash_raster_state()->last_result == SPLASH_RASTER_NO_SYNC);
    REQUIRE(clock_us <= 100010 && irq_on);
    setup(1, 0); frozen = missing_v = 1;
    REQUIRE(pc98_splash_raster_state()->last_result == SPLASH_RASTER_NO_SYNC);
    REQUIRE(calls <= 1000010);
    setup(1, 0); clock_error = 1;
    REQUIRE(pc98_splash_raster_state()->last_result == SPLASH_RASTER_NO_SYNC);
    setup(1, 0); backwards = 1;
    REQUIRE(pc98_splash_raster_state()->last_result == SPLASH_RASTER_NO_SYNC);

    setup(1, 0); make_logo();
    struct SplashRaster *s = pc98_splash_raster_state();
    missing_h = 1; track_irq = 1;
    REQUIRE(!pc98_splash_raster_table(s, &logo));
    REQUIRE(s->late && s->unapplied && irq_on && max_closed * den <= 2 * 100000);
    REQUIRE(min_open * den >= 100000);
    missing_h = 0; writes = 0;
    REQUIRE(!pc98_splash_raster_table(s, &logo) && writes == 400);

    /* An IRQ crossing the predicted falling edge by half a line discards
     * this frame, then the next new VSYNC recovers with line-0 color. */
    setup(1, 0); make_logo(); s = pc98_splash_raster_state();
    unsigned long long next_rise = ((clock_us * den / (total * 100000)) + 1) * total * 100000 / den;
    inject_at = next_rise + s->high_us - 40;
    inject_us = 70;
    REQUIRE(!pc98_splash_raster_table(s, &logo));
    REQUIRE(s->dropped == 1 && writes == 4 && irq_on);
    writes = 0;
    REQUIRE(!pc98_splash_raster_table(s, &logo) && writes == 400);
    REQUIRE(s->dropped == 1);

    /* A delayed rising-edge observation must not turn an already-low VSYNC
     * into a captured falling edge and shift every band in the frame. */
    setup(0, 0); make_logo(); s = pc98_splash_raster_state();
    next_rise = ((clock_us * den / (total * 100000)) + 1) * total * 100000 / den;
    inject_at = next_rise - 1;
    inject_us = 200;
    REQUIRE(!pc98_splash_raster_table(s, &logo));
    REQUIRE(s->dropped == 1 && writes == 4 && irq_on);
    REQUIRE(s->frame == 1 && s->unapplied == 99 && s->dynamic_ok);
    writes = 0;
    REQUIRE(!pc98_splash_raster_table(s, &logo) && writes == 400);
    REQUIRE(s->dropped == 1 && s->unapplied == 99 && irq_on);

    setup(0, 0); make_logo(); s = pc98_splash_raster_state();
    track_irq = 1;
    next_rise = ((clock_us * den / (total * 100000)) + 1) * total * 100000 / den;
    inject_at = next_rise + s->high_us + 70;
    inject_us = 140;
    REQUIRE(!pc98_splash_raster_table(s, &logo));
    REQUIRE(s->late && irq_on && s->dropped == 0);
    REQUIRE(min_open * den >= 100000);

    setup(0, 0); make_logo(); s = pc98_splash_raster_state();
    track_irq = 1; logo.count = 5;
    next_rise = ((clock_us * den / (total * 100000)) + 1) * total * 100000 / den;
    inject_at = next_rise + s->high_us + 70;
    inject_us = 510;
    REQUIRE(!pc98_splash_raster_table(s, &logo));
    REQUIRE(writes == 12 && s->unapplied == 2 && s->late == 4);
    REQUIRE(write_value[6] == logo.entries[3].r);
    REQUIRE(write_value[10] == logo.entries[4].r);
    REQUIRE(min_open * den >= 100000 && irq_on);
}

static void fixed_palette(void)
{
    setup(0, 0); high_lines = 15;
    struct SplashRaster *s = pc98_splash_raster_state();
    REQUIRE(!s->dynamic_ok);
    for (u32 i = 0; i < 16; i++) fixed.pal[i] = (i << 8) | ((15-i) << 4) | (i ^ 7);
    REQUIRE(!pc98_splash_raster_frame(s, &fixed));
    REQUIRE(writes == 64 && irq_on && !protected_writes);
    ports();
    for (u32 i = 0; i < 16; i++) {
        REQUIRE(write_value[4*i] == i);
        REQUIRE(write_value[4*i+1] == 15-i);
        REQUIRE(write_value[4*i+2] == i);
        REQUIRE(write_value[4*i+3] == (i ^ 7));
    }
    fixed.count = 1; writes = 0;
    REQUIRE(pc98_splash_raster_frame(s, &fixed) == SPLASH_RASTER_NO_SYNC && !writes);
    fixed.count = 0; irq_on = 0;
    REQUIRE(!pc98_splash_raster_frame(s, &fixed) && !irq_on);
}

/* No PlPlan/PlWindow is consulted by the scan oscillator or palette latch.
 * Each visible-line start takes a snapshot of all 48 DAC components and
 * their last effective OUT timestamps, even when the player drops a row. */
static void scan_step(unsigned long long delta)
{
    REQUIRE(++calls < 4000000);
    unsigned long long end = scan.time + delta;
    if (irq_on && scan.input.irq_cost && end >= scan.irq_at) {
        end += scan.input.irq_cost;
        scan.irq_at += (unsigned long long)total * scan.input.h;
        scan.irq_count++;
    }
    if (irq_on && scan.input.delay_cost && end >= scan.input.delay_at) {
        end += scan.input.delay_cost;
        scan.input.delay_cost = 0;
        scan.irq_count++;
    }
    while (scan.sampling && scan.next_visible <= end) {
        if (!scan.row) scan.first_visible = scan.next_visible;
        for (u32 i = 0; i < 16; i++) for (u32 c = 0; c < 3; c++) {
            scan.visible[scan.row][i][c] = scan.palette[i][c];
            scan.effective[scan.row][i][c] = scan.changed[i][c];
        }
        scan.last_visible = scan.next_visible;
        scan.rows++;
        if (++scan.row == 400) {
            scan.row = 0;
            scan.next_visible += (unsigned long long)(total - 399) * scan.input.h;
        } else scan.next_visible += scan.input.h;
    }
    scan.time = end;
}

static unsigned int scan_status(void)
{
    scan_step(scan.input.in_cost + (irq_on ? 0 : scan.input.branch_cost));
    if (!irq_on) scan.scan_reads++;
    unsigned long long period = (unsigned long long)total * scan.input.h;
    unsigned long long phase = scan.time % period;
    unsigned int status = 0;
    if (!missing_v && phase < (unsigned long long)high_lines * scan.input.h)
        status |= GDC_STAT_VSYNC;
    int high = (phase + 2 * SCAN_DEN) % scan.input.h >= scan.input.h - scan.input.blank;
    if (scan.input.lost_h && scan.time >= scan.input.lost_h &&
        scan.time < scan.input.lost_h + scan.input.blank) high = 0;
    if (scan.input.early_h && scan.time >= scan.input.early_h &&
        scan.time < scan.input.early_h + scan.input.blank) high = 1;
    if (!missing_h && high) status |= GDC_STAT_HBLANK;
    return status;
}

static void scan_out(unsigned int port, unsigned int value)
{
    REQUIRE(writes < HOST_WRITES && value < 16);
    scan_step(scan.input.out_cost);
    scan.out_at[writes] = scan.time;
    scan.out_if[writes] = irq_on;
    write_time[writes] = scan.time / SCAN_DEN;
    write_port[writes] = port;
    write_value[writes++] = value;
    if (irq_on) open_writes++; else protected_writes++;
    if (port == PAL_IDX_PORT) scan.index = value;
    else {
        u32 component = port == PAL_R_PORT ? 0 : port == PAL_G_PORT ? 1 : 2;
        REQUIRE(port == PAL_R_PORT || port == PAL_G_PORT || port == PAL_B_PORT);
        scan.palette[scan.index][component] = value;
        scan.changed[scan.index][component] = scan.time;
    }
}

static struct pit_setup host_pit = {
    .clk_hz = 2457600, .hz = 100, .reload = 24576,
    .period_us = 10000, .valid = 1
};
const struct pit_setup *pit_get_setup(void) { return &host_pit; }

/* The PIT service is a boundary fixture, not an implementation of P2.
 * It observes independent wall time, IRQ state and the player's supplied
 * interval. V4/V5 own PIT arithmetic/re-latching and product settlement. */
int ktime_splash_sample(struct TimeSplashSample *sample)
{
    REQUIRE(scan.enabled && !irq_on);
    scan.samples++;
    if (scan.sample_error) return -1;
    unsigned long long cycles = scan.time * host_pit.clk_hz /
                                ((unsigned long long)SCAN_DEN * USEC_PER_SEC);
    sample->count = host_pit.reload - cycles % host_pit.reload;
    sample->pending = scan.samples % 2 ? 0 : 1;
    if (scan.samples % 2) scan.sample_at = scan.time;
    return 0;
}
int ktime_splash_settle(const struct TimeSplashMathInput *input)
{
    REQUIRE(scan.enabled && !irq_on && scan.samples == 2 && !scan.settles);
    scan.settles++;
    REQUIRE(input->reload == host_pit.reload);
    unsigned long long cycles = (scan.time - scan.sample_at) * host_pit.clk_hz /
                                ((unsigned long long)SCAN_DEN * USEC_PER_SEC);
    REQUIRE(input->elapsed_min <= cycles && cycles <= input->elapsed_max);
    return scan.settle_error ? -1 : 0;
}

/* Freestanding host compiler may lower large zero initializers to memset. */
void *memset(void *buffer, int value, u32 size)
{
    volatile u8 *p = buffer;
    for (u32 i = 0; i < size; i++) p[i] = value;
    return buffer;
}

static struct OsriImage image;
static struct PlPlan plan;
static struct PlProfile fixture;

static struct PlCost host_cost(u32 min, u32 max)
{
    return (struct PlCost){ SCAN_DEN, { min, max } };
}
static struct PlWindow host_window(u32 lower, u32 upper, u32 base_min,
                                    u32 base_max, u32 step)
{
    return (struct PlWindow){ SCAN_DEN, lower, upper,
                             {base_min, base_max}, {step, step} };
}

/* Synthetic certified inputs, never Ra266 measurements. Integer 32/40us
 * periods are within the accepted +/-1% of nominal 31/24k. Bounds include
 * the separate IN/branch/OUT costs above and every adjacent count pair. */
static void image_fixture(int mode, int vs)
{
    setup(mode, vs);
    struct SplashRaster *s = pc98_splash_raster_state();
    REQUIRE(s->dynamic_ok);
    scan = (struct HostScan){0};
    scan.enabled = 1;
    scan.input = (struct HostScanInput){ .h = (mode ? 32 : 40) * SCAN_DEN,
        .blank = 6 * SCAN_DEN, .in_cost = 25, .branch_cost = 175,
        .out_cost = 10, .clock_cost = 25, .cli_clock_cost = 100 };
    u32 h = scan.input.h;
    if (!vs) {
        /* A permitted IRQ before F moves entry later within its legal
         * interval. The event follows the oscillator, not player calls. */
        scan.input.irq_at = (unsigned long long)high_lines * h - h - 8 * SCAN_DEN;
        scan.input.irq_cost = 11 * SCAN_DEN;
        scan.irq_at = scan.input.irq_at;
    }
    fixture = (struct PlProfile){ .n = 5 };
    for (u32 i = 0; i <= 5; i++) {
        fixture.count[i].io = (struct SplashRasterImageBudget){
            .den = SCAN_DEN, .outs = i, .q = {200, 300}, .d = {0, 0},
            .w = {i * 10, i * 10}, .h = {h, h}, .b = {600, 600}, .error = 0 };
        fixture.count[i].prepare = host_cost(0, 185);
        fixture.count[i].edge = host_window((h - 200) / 200,
                                             h / 200 + 3, 200, 260, 200);
    }
    for (u32 i = 0; i < PL_EXTRA_COUNT; i++) fixture.extra[i] = host_cost(0, 0);
    fixture.extra[PL_OPEN_POLL] = host_cost(25, 1175);
    fixture.extra[PL_C64] = host_cost(0, 64 * 35 + 100);
    fixture.extra[PL_CLI_HEAD] = host_cost(0, 200);
    fixture.extra[PL_FIRST_PHASE] = host_cost(h - 800, h - 800);
    fixture.extra[PL_FRAME_REMAIN] = host_cost(0, 402 * h);
    fixture.extra[PL_COMMON_EXIT] = host_cost(0, 1000);
    fixture.extra[PL_CEXIT] = host_cost(0, 1000);
    fixture.extra[PL_LATCH_CLOCK] = host_cost(100, 100);
    fixture.extra[PL_CLI_CLOCK] = host_cost(100, 100);
    for (u32 i = 0; i < PL_INITIAL_OUTS; i++)
        fixture.remaining[i] = host_cost(0, (64 - i) * 35 + 50);
    for (u32 i = 0; i < PL_EXIT_COUNT; i++) fixture.exit[i] = host_cost(0, 0);
    fixture.first = vs ? host_window((h - 800) / 200, h / 200 - 1, 100, 200, 200) :
                         host_window((h - 1000) / 200, h / 200, 100, 210, 200);
    fixture.capture_f = host_window((h - 400) / 200, h / 200 - 1, 100, 200, 200);
    fixture.end_low = host_window(1, 6, 200, 200, 200);
    REQUIRE(qualified(&fixture, s));
    pl_profiles[mode][vs][0] = fixture;
    calls = 0;
}

/* Palette history is generated from the file events, without using the
 * product's prepared ports/counts. The six-row cycle exercises counts
 * 0..5, adjacent changes, 1/2/3 indices, and all RGB component masks. */
static void image_make(void)
{
    image = (struct OsriImage){ .version = OSRI_VERSION_2, .band = 1 };
    u16 pal[16];
    for (u32 i = 0; i < 16; i++) pal[i] = image.pal[i] = (i << 8) | ((15-i) << 4) | (i ^ 7);
    for (u32 y = 1; y < 400; y++) {
        u32 count = y % 6;
        u32 slots = count > 3 ? (count == 5 ? 3 : 2) : !!count;
        for (u32 j = 0; j < slots; j++) {
            /* Keep event indices sorted, as the loader requires. */
            u32 index = (y % 5) * 3 + j;
            u16 mask = count == 1 ? (u16)(0xf << (4 * (y % 3))) :
                       count == 2 || (count == 4 && !j) ?
                           (u16)(0xfff ^ (0xf << (4 * ((y / 6) % 3)))) :
                       count == 3 ? 0xfff : (u16)(0xf << (4 * ((y + j) % 3)));
            pal[index] ^= mask;
            image.event[image.count++] = (struct OsriEvent){
                .line = y, .index = index, .rgb444 = pal[index] };
        }
    }
}

static void product_profiles(void)
{
    /* Run before overwriting ANY profile. Future R1a/R1b initializers are
     * checked through the same real qualifier; den=0 must always reject. */
    for (u32 mode = 0; mode < 2; mode++) for (u32 vs = 0; vs < 2; vs++) {
        setup(mode, vs);
        struct SplashRaster *s = pc98_splash_raster_state();
        for (u32 slot = 0; slot < 2; slot++) {
            const struct PlProfile *p = &pl_profiles[mode][vs][slot];
            int accepted = qualified(p, s);
            if (!p->count[0].io.den || p->n != PL_ADOPTED_OUTS) REQUIRE(!accepted);
            int rc = pl_prepare(&image, p->n, &plan);
            REQUIRE((rc == 0) == accepted);
            REQUIRE(!writes);
        }
    }
}

static void image_observe(struct SplashRaster *s, int complete)
{
    u16 expected[16];
    for (u32 i = 0; i < 16; i++) expected[i] = image.pal[i];
    u32 expected_writes = 64;
    for (u32 i = 0; i < image.count; i++) {
        const struct OsriEvent *e = &image.event[i];
        u16 changed = expected[e->index] ^ e->rgb444;
        expected_writes += 1 + !!(changed & 0xf00) + !!(changed & 0xf0) + !!(changed & 0xf);
        expected[e->index] = e->rgb444;
    }
    if (complete) REQUIRE(writes == expected_writes);
    for (u32 i = 0; i < 16; i++) {
        expected[i] = image.pal[i];
        REQUIRE(write_port[4*i] == PAL_IDX_PORT && write_value[4*i] == i);
        REQUIRE(write_port[4*i+1] == PAL_G_PORT && write_value[4*i+1] == ((image.pal[i] >> 4) & 15));
        REQUIRE(write_port[4*i+2] == PAL_R_PORT && write_value[4*i+2] == (image.pal[i] >> 8));
        REQUIRE(write_port[4*i+3] == PAL_B_PORT && write_value[4*i+3] == (image.pal[i] & 15));
        REQUIRE(scan.out_at[4*i+3] < scan.first_visible);
    }
    u32 event = 0;
    REQUIRE(scan.rows >= (complete ? 400u : 1u) && scan.rows <= 400);
    for (u32 y = 0; y < (complete ? 400u : scan.rows); y++) {
        while (event < image.count && image.event[event].line == y) {
            expected[image.event[event].index] = image.event[event].rgb444;
            event++;
        }
        for (u32 i = 0; i < 16; i++) for (u32 c = 0; c < 3; c++) {
            u32 shift = c == 0 ? 8 : c == 1 ? 4 : 0;
            REQUIRE(scan.visible[y][i][c] == ((expected[i] >> shift) & 15));
            REQUIRE(scan.effective[y][i][c] <= scan.first_visible + (unsigned long long)y * scan.input.h);
        }
    }
    for (u32 i = 64; i < writes; i++) if (write_port[i] != PAL_IDX_PORT) {
        unsigned long long phase = scan.out_at[i] % scan.input.h;
        REQUIRE(phase >= scan.input.h - scan.input.blank - 2 * SCAN_DEN &&
                phase < scan.input.h - 2 * SCAN_DEN);
        REQUIRE(!scan.out_if[i]);
    }
    REQUIRE(irq_on && scan.samples == 2 && scan.settles == 1 && !scan.scan_clocks);
    REQUIRE(max_closed < 20000 * SCAN_DEN);
    if (complete) {
        REQUIRE(closed_at < scan.first_visible);
        REQUIRE(scan.time >= scan.first_visible + 400 * scan.input.h - 2 * SCAN_DEN);
    }
    REQUIRE(s->late == s->unapplied);
}

static void host_write(const char *text, u32 len)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(text), "d"(len) : "memory");
}
static void host_number(const char *name, u32 value)
{
    u32 len = 0;
    while (name[len]) len++;
    char hex[] = "=00000000\n";
    for (u32 i = 0; i < 8; i++) hex[i+1] = "0123456789abcdef"[(value >> (28-4*i)) & 15];
    host_write(name, len);
    host_write(hex, sizeof(hex)-1);
}

static void image_arm(unsigned long long visible)
{
    scan.next_visible = visible;
    scan.sampling = 1;
    scan.rows = scan.row = scan.samples = scan.settles = 0;
    scan.scan_reads = scan.scan_clocks = 0;
    writes = protected_writes = open_writes = 0;
    max_closed = 0;
    calls = 0;
}

/* Validate columns using component popcounts and DAC state, independently
 * of the product's append/emit functions and its event search. */
static void image_plan(void)
{
    u16 expected[16], actual[16];
    for (u32 i = 0; i < 16; i++) actual[i] = expected[i] = image.pal[i];
    u32 event = 0, covered = 0;
    for (u32 y = 0; y < 400; y++) {
        u32 slots = 0, components = 0;
        while (event < image.count && image.event[event].line == y) {
            const struct OsriEvent *e = &image.event[event++];
            u16 changed = expected[e->index] ^ e->rgb444;
            components += !!(changed & 0xf00) + !!(changed & 0x0f0) + !!(changed & 0x00f);
            expected[e->index] = e->rgb444;
            slots++;
        }
        const struct PlRow *r = &plan.row[y];
        REQUIRE(r->count == components + (slots ? slots - 1 : 0));
        covered |= 1u << r->count;
        u32 index = r->first;
        for (u32 j = 0; j < r->count; j++) {
            u32 port = r->port[j], value = r->value[j];
            if (port == PAL_IDX_PORT) index = value;
            else {
                u32 shift = port == PAL_R_PORT ? 8 : port == PAL_G_PORT ? 4 : 0;
                REQUIRE(port == PAL_R_PORT || port == PAL_G_PORT || port == PAL_B_PORT);
                actual[index] = (actual[index] & ~(15u << shift)) | (value << shift);
            }
        }
        for (u32 i = 0; i < 16; i++) REQUIRE(actual[i] == expected[i]);
    }
    REQUIRE(covered == 0x3f);
}

static void image_replay(void)
{
    product_profiles();
    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++) {
        image_fixture(mode, vs); image_make();
        struct SplashRaster *s = pc98_splash_raster_state();
        REQUIRE(!pl_prepare(&image, 5, &plan));
        image_plan();
        unsigned long long period = (unsigned long long)total * scan.input.h;
        /* Diagnostic length, with one unchanged absolute deadline. */
        unsigned long long visible = (vs ? period : 2 * period) +
            (unsigned long long)(high_lines + s->active0_lines) * scan.input.h;
        for (u32 frame = 0; frame < 300; frame++, visible += period) {
            image_arm(visible);
            int rc = pl_frame(s, &image, &plan, 10000000);
            REQUIRE(rc == PL_FRAME_OK);
            REQUIRE(s->frame == frame + 1 && !s->dropped && !s->late && !s->unapplied);
            image_observe(s, 1);
#ifdef PL_HOST_TRACE
            /* Optional evidence for V3: a 16*u32 little-endian header,
             * then DAC values, component timestamps, OUT timestamps,
             * ports, values and saved IF (array lengths from the header).
             * Only the first frame per mode/phase is exported. */
            if (!frame) {
                const u32 header[] = { 0x32564c50, mode, vs, writes, 400, 16, 3,
                    SCAN_DEN, scan.input.h, scan.input.blank, scan.input.in_cost,
                    scan.input.branch_cost, scan.input.out_cost,
                    scan.input.clock_cost, scan.input.cli_clock_cost, 0 };
                host_write((const char *)header, sizeof(header));
                host_write((const char *)scan.visible, sizeof(scan.visible));
                host_write((const char *)scan.effective, sizeof(scan.effective));
                host_write((const char *)scan.out_at, writes * sizeof(scan.out_at[0]));
                host_write((const char *)write_port, writes * sizeof(write_port[0]));
                host_write((const char *)write_value, writes * sizeof(write_value[0]));
                host_write((const char *)scan.out_if, writes);
            }
#endif
        }
        if (!vs) REQUIRE(scan.irq_count > 0);
        host_number("GREEN mode/phase", mode * 2 + vs);
        host_number("frames", s->frame);
        host_number("rows/frame", scan.rows);
        host_number("OUTs/frame", writes);
        host_number("components checked/frame", 400 * 16 * 3);
        host_number("inner clock reads", scan.scan_clocks);
        host_number("first visible us", scan.first_visible / SCAN_DEN);
        host_number("last visible us", scan.last_visible / SCAN_DEN);
        host_number("last component OUT us", scan.out_at[writes-1] / SCAN_DEN);
        host_number("max IF=0 us", max_closed / SCAN_DEN);
    }
}

static unsigned long long image_first_visible(const struct SplashRaster *s)
{
    unsigned long long period = (unsigned long long)total * scan.input.h;
    return (s->active0_lines ? period : 2 * period) +
        (unsigned long long)(high_lines + s->active0_lines) * scan.input.h;
}

static void image_faults(void)
{
    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++) {
        image_fixture(mode, vs); image_make();
        struct SplashRaster *s = pc98_splash_raster_state();
        REQUIRE(!pl_prepare(&image, 5, &plan));
        unsigned long long visible = image_first_visible(s);
        unsigned long long period = (unsigned long long)total * scan.input.h;
        unsigned long long lost = visible + 123 * scan.input.h - 8 * SCAN_DEN;
        scan.input.lost_h = lost;
        image_arm(visible);
        REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_DROP);
        u32 unapplied = 0;
        for (u32 i = 0; i < image.count; i++) if (image.event[i].line >= 123) unapplied++;
        REQUIRE(s->dropped == 1 && s->late == unapplied && s->unapplied == unapplied);
        for (u32 i = 64; i < writes; i++) if (write_port[i] != PAL_IDX_PORT)
            REQUIRE(scan.out_at[i] < lost);
        /* No next-edge emission or carry to row124 after a missing H. */
        REQUIRE(scan.time < lost + scan.input.h);
        image_observe(s, 0);
        scan.input.lost_h = 0;
        image_arm(visible + period);
        REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_OK);
        image_observe(s, 1);
        REQUIRE(s->dropped == 1 && s->late == unapplied && s->unapplied == unapplied);
        host_number("GREEN missing-H/drop/recovery mode/phase", mode * 2 + vs);
        host_number("late=unapplied", unapplied);

        /* A palette-time IRQ crosses Linit: partial initial writes stop,
         * and the following frame must restore all 16 colors again. */
        image_fixture(mode, vs); image_make(); s = pc98_splash_raster_state();
        REQUIRE(!pl_prepare(&image, 5, &plan));
        visible = image_first_visible(s);
        scan.input.delay_at = visible - (unsigned long long)(vs ? s->active0_lines : high_lines) * scan.input.h + 500;
        scan.input.delay_cost = (unsigned long long)(vs ? s->active0_lines : high_lines) * scan.input.h;
        REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_DROP);
        REQUIRE(writes > 0 && writes < 64 && irq_on && !scan.samples && s->dropped == 1);
        image_arm(visible + period);
        REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_OK);
        image_observe(s, 1);
        REQUIRE(s->dropped == 1 && s->unapplied == image.count);

        image_fixture(mode, vs); image_make(); s = pc98_splash_raster_state();
        REQUIRE(!pl_prepare(&image, 5, &plan));
        visible = image_first_visible(s);
        scan.input.early_h = visible + 123 * scan.input.h - 18 * SCAN_DEN;
        image_arm(visible);
        REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_DROP);
        REQUIRE(s->dropped == 1 && s->late == unapplied && s->unapplied == unapplied);
        for (u32 i = 64; i < writes; i++) if (write_port[i] != PAL_IDX_PORT)
            REQUIRE(scan.out_at[i] < scan.input.early_h);
        image_observe(s, 0);
    }

    for (int mode = 0; mode < 2; mode++) {
        image_fixture(mode, 0); image_make();
        struct SplashRaster *state = pc98_splash_raster_state();
        REQUIRE(!pl_prepare(&image, 5, &plan));
        unsigned long long start = image_first_visible(state);
        scan.input.delay_at = start - scan.input.h - 8 * SCAN_DEN;
        scan.input.delay_cost = scan.input.h + 20 * SCAN_DEN;
        REQUIRE(pl_frame(state, &image, &plan, 3000000) == PL_FRAME_DROP);
        REQUIRE(writes == 64 && !scan.samples && irq_on && state->dropped == 1);
        REQUIRE(state->late == image.count && state->unapplied == image.count);
    }

    image_fixture(1, 1); image_make();
    struct SplashRaster *s = pc98_splash_raster_state();
    REQUIRE(!pl_prepare(&image, 5, &plan));
    missing_h = 1;
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_DROP);
    REQUIRE(writes == 64 && irq_on && s->dropped == 1 && s->late == image.count);
    REQUIRE(scan.scan_reads < 100 && scan.samples == 2 && scan.settles == 1);

    image_fixture(1, 1); image_make(); s = pc98_splash_raster_state();
    REQUIRE(!pl_prepare(&image, 5, &plan));
    REQUIRE(pl_frame(s, &image, &plan, 1) == PL_FRAME_DEADLINE);
    REQUIRE(!writes && !s->frame && irq_on);
    missing_v = 1;
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_NO_SYNC);
    REQUIRE(scan.time <= 100002 * SCAN_DEN && !writes && irq_on);

    image_fixture(1, 1); image_make(); s = pc98_splash_raster_state();
    REQUIRE(!pl_prepare(&image, 5, &plan));
    frozen = missing_v = 1;
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_NO_SYNC);
    REQUIRE(calls < 2000000 && !writes && irq_on);

    image_fixture(0, 1); image_make(); s = pc98_splash_raster_state();
    REQUIRE(!pl_prepare(&image, 5, &plan));
    irq_on = 0;
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_NO_SYNC);
    REQUIRE(!irq_on && !writes);
    irq_on = 1; clock_error = 1;
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_NO_SYNC);
    REQUIRE(!writes && irq_on);

    image_fixture(1, 1); image_make(); s = pc98_splash_raster_state();
    REQUIRE(!pl_prepare(&image, 5, &plan));
    backwards = 1;
    REQUIRE(pl_frame(s, &image, &plan, 20000000) == PL_FRAME_NO_SYNC);
    REQUIRE(!writes && irq_on);

    /* Settlement failure consumes the pair and preserves drop accounting,
     * but forces common deadline exit instead of inventing a next frame. */
    image_fixture(1, 1); image_make(); s = pc98_splash_raster_state();
    REQUIRE(!pl_prepare(&image, 5, &plan));
    unsigned long long visible = image_first_visible(s);
    image_arm(visible);
    scan.settle_error = 1;
    scan.input.lost_h = visible + 123 * scan.input.h - 8 * SCAN_DEN;
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_DEADLINE);
    REQUIRE(irq_on && scan.settles == 1 && s->dropped == 1 && s->late == s->unapplied);
}

static void image_rejections(void)
{
    image_fixture(1, 1); image_make();
    struct SplashRaster *s = pc98_splash_raster_state();
    struct PlProfile *p = &pl_profiles[1][1][0];
    /* Unset and partially installed inputs must fail before plan changes
     * or palette OUTs. Test the equality boundary of the physical budget. */
    plan.row[0].count = 99;
    p->count[0].io.den = 0;
    REQUIRE(pl_prepare(&image, 5, &plan) == -1 && plan.row[0].count == 99);
    *p = fixture; p->remaining[63].den = 0;
    REQUIRE(pl_prepare(&image, 5, &plan) == -1 && plan.row[0].count == 99);
    *p = fixture; p->exit[PL_EXIT_DROP].den++;
    REQUIRE(pl_prepare(&image, 5, &plan) == -1 && plan.row[0].count == 99);
    *p = fixture; p->count[5].io.b.min = p->count[5].io.q.max + p->count[5].io.w.max;
    REQUIRE(pl_prepare(&image, 5, &plan) == -1 && plan.row[0].count == 99);
    *p = fixture;
    REQUIRE(pl_prepare(&image, 6, &plan) == -1 && !writes);
    REQUIRE(qualified(p, s));
    image = (struct OsriImage){ .version = OSRI_VERSION_1, .count = 2 };
    image.event[0] = (struct OsriEvent){ .line = 1, .index = 0, .rgb444 = 0xfff };
    image.event[1] = (struct OsriEvent){ .line = 1, .index = 1, .rgb444 = 0xfff };
    REQUIRE(pl_prepare(&image, 5, &plan) == -1 && !writes); /* 7 OUTs */

    /* n=0 presents initial16 with IF open and never enters PIT protection. */
    image.count = 0;
    REQUIRE(!pl_prepare(&image, 5, &plan));
    REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_OK);
    REQUIRE(writes == 64 && !protected_writes && !scan.samples && irq_on);
}

/* Exercise the real continuous wait over every 1/100us start phase at the
 * measured-like 31.8us / 5.9us waveform. No clock read advances these loops. */
static void image_burst(void)
{
    image_fixture(1, 1);
    scan.input.h = 3180;
    scan.input.blank = 590;
    const struct PlRow empty = {0};
    const struct PlWindow first = host_window(1, 22, 200, 200, 200);
    const struct PlWindow next = host_window(15, 19, 200, 200, 200);
    for (u32 phase = 0; phase < scan.input.h; phase++) {
        scan.time = phase;
        calls = 0;
        irq_on = 0;
        u32 iterations;
        REQUIRE(!next_edge(&first, &empty, emit0, 0, &iterations));
        unsigned long long previous = (scan.time + 800) / scan.input.h;
        for (u32 edge = 0; edge < 64; edge++) {
            REQUIRE(!next_edge(&next, &empty, emit0, 0, &iterations));
            unsigned long long current = (scan.time + 800) / scan.input.h;
            REQUIRE(current == previous + 1);
            previous = current;
        }
    }
    irq_on = 1;
    host_number("GREEN 31.8/5.9us start phases x64", scan.input.h);

    /* The same oscillator must also drive the REAL frame player, with
     * nonempty columns and matching budget inputs. Sweep every 1/100us
     * entry phase in 31k VS mode, without phase sampling. The four normal
     * mode/phase combinations remain covered by image_replay(). */
    image_fixture(1, 1); image_make();
    struct SplashRaster *s = pc98_splash_raster_state();
    scan.input.h = 3180;
    scan.input.blank = 590;
    for (u32 count = 0; count <= 5; count++) {
        fixture.count[count].io.h = (PlRange){3180, 3180};
        fixture.count[count].io.b = (PlRange){590, 590};
    }
    /* E(1) is H - blank - the oscillator's 2us offset after F. The first
     * IN returns after the same 200 units as each subsequent iteration. */
    fixture.extra[PL_FIRST_PHASE] = host_cost(2390, 2390);
    fixture.extra[PL_FRAME_REMAIN] = host_cost(0, 402 * 3180);
    fixture.first = host_window(11, 15, 200, 200, 200);
    REQUIRE(qualified(&fixture, s));
    pl_profiles[1][1][0] = fixture;
    for (u32 phase = 0; phase < 3180; phase++) {
        /* Reset the real acquisition context for every phase. Reuse only
         * mode qualification and fixed inputs, not an earlier F anchor. */
        REQUIRE(!pl_prepare(&image, 5, &plan));
        scan.time = phase;
        unsigned long long visible = image_first_visible(s);
        image_arm(visible);
        REQUIRE(pl_frame(s, &image, &plan, 3000000) == PL_FRAME_OK);
        REQUIRE(s->frame == phase + 1 && !s->dropped && !s->late && !s->unapplied);
        image_observe(s, 1);

        /* Match every emitted component to its FILE event's row, port,
         * value and HBLANK interval, independently of the prepared plan. */
        u16 palette[16];
        for (u32 i = 0; i < 16; i++) palette[i] = image.pal[i];
        u32 out = 64;
        const u32 ports[] = {PAL_G_PORT, PAL_R_PORT, PAL_B_PORT};
        const u32 shifts[] = {4, 8, 0};
        for (u32 i = 0; i < image.count; i++) {
            const struct OsriEvent *e = &image.event[i];
            u16 changed = palette[e->index] ^ e->rgb444;
            REQUIRE(write_port[out] == PAL_IDX_PORT && write_value[out] == e->index);
            out++;
            unsigned long long row = visible + (unsigned long long)e->line * scan.input.h;
            for (u32 c = 0; c < 3; c++) if (changed & (15u << shifts[c])) {
                REQUIRE(out < writes && write_port[out] == ports[c]);
                REQUIRE(write_value[out] == ((e->rgb444 >> shifts[c]) & 15));
                REQUIRE(scan.out_at[out] >= row - 590 - 2 * SCAN_DEN);
                REQUIRE(scan.out_at[out] < row - 2 * SCAN_DEN);
                out++;
            }
            palette[e->index] = e->rgb444;
        }
        REQUIRE(out == writes);
    }
    host_number("GREEN 31.8/5.9us full-frame start phases", 3180);
    host_number("rows/frame", 400);
}

void _start(void)
{
    qualification(); replay(); faults(); fixed_palette(); image_replay(); image_faults(); image_rejections(); image_burst(); finish(0);
}
