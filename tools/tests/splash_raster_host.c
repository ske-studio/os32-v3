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
static unsigned long long write_time[1024];
static u32 write_port[1024], write_value[1024];

static void finish(int code)
{
    if (code) {
        char text[] = "clock=00000000 writes=00000000 closed=00000000\n";
        u32 values[] = {(u32)clock_us, writes, (u32)max_closed};
        const int offsets[] = {6, 22, 38};
        for (int i = 0; i < 3; i++) for (int j = 0; j < 8; j++)
            text[offsets[i]+j] = "0123456789abcdef"[(values[i] >> (28 - 4*j)) & 15];
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(text), "d"(sizeof(text)-1) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(c) do { if (!(c)) finish(__LINE__ % 250 + 1); } while (0)

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
    closed_at = clock_us;
    return saved;
}
static void irq_restore(unsigned int saved)
{
    REQUIRE(!irq_on);
    unsigned long long duration = clock_us - closed_at;
    if (duration > max_closed) max_closed = duration;
    irq_on = saved;
    opened_at = clock_us;
}
static void outp(unsigned int port, unsigned int value)
{
    REQUIRE(writes < 1024);
    write_time[writes] = clock_us;
    write_port[writes] = port;
    write_value[writes++] = value;
    if (irq_on) open_writes++; else protected_writes++;
    clock_us++; /* nonzero OUT cost, including four-OUT critical sections */
}

#include "../../drivers/video/pc98/splash_raster.c"

static void setup(int mode, int vs_only)
{
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

void _start(void)
{
    qualification(); replay(); faults(); fixed_palette(); finish(0);
}
