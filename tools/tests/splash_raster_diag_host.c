/* Real player with diagnostics; the original suite still runs without them. */
#define _start raster_original_start
#include "splash_raster_host.c"
#undef _start

/* 0.1 us ticks: independently sample A0h at 1 us intervals, including a
 * 5.9 us H pulse every 31.8 us. Chunk boundaries are bookkeeping only;
 * they do not open IF or reset the continuous detector. */
static void continuous_h_model(void)
{
    enum { H = 318, WIDTH = 59, SAMPLE = 10, CHUNK = 18, PERIODS = 64,
           SAMPLES = (65 * H + SAMPLE - 1) / SAMPLE + 1 };
    for (u32 phase = 0; phase < H; phase++) {
        u32 armed = 0, old_armed = 0, rises = 0, old_rises = 0;
        for (u32 i = 0; i < SAMPLES; i++) {
            u32 time = phase + i * SAMPLE;
            u32 status = (time % H >= H - WIDTH ? GDC_STAT_HBLANK : 0) |
                         (time % (H * 525) < H * 25 ? GDC_STAT_VSYNC : 0);
            if (i % CHUNK == 0) old_armed = 0;
            if (!(status & GDC_STAT_HBLANK)) {
                armed = old_armed = 1;
            } else {
                if (armed) { rises++; armed = 0; }
                if (old_armed) { old_rises++; old_armed = 0; }
            }
        }
        REQUIRE(rises >= PERIODS + 1);
        REQUIRE(old_rises < rises);
        REQUIRE((SAMPLES - 1) * SAMPLE <= 66 * H);
    }
    static const char log[] = "GREEN continuous A0h model: 64 periods, short chunks retain armed, IF0 <= 66H\n";
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(log), "d"(sizeof(log)-1) : "memory");
}

void _start(void)
{
    continuous_h_model();
    for (int mode = 0; mode < 2; mode++) {
        setup(mode, 0); make_logo();
        struct SplashRaster *s = pc98_splash_raster_state();
        logo.count = 3;
        logo.entries[1].line = 20;
        logo.entries[2].line = 398;
        REQUIRE(!pc98_splash_raster_table(s, &logo));
        const struct SplashRasterDiag *d = pc98_splash_raster_diag();
        REQUIRE(d->count == 3 && d->capacity >= 100 && !d->overflow && !d->clock_errors);
        REQUIRE(d->if0_max_us > 0 && d->if0_total_us >= d->if0_max_us);
        REQUIRE(d->if0_max_us <= max_closed && max_closed - d->if0_max_us <= 1);
        for (u32 i = 0; i < d->count; i++) {
            const struct SplashRasterEventDiag *e = &d->events[i];
            REQUIRE(e->line == logo.entries[i].line && e->index == 1 && e->applied);
            REQUIRE(e->begin_us <= write_time[4*i] && e->end_us > write_time[4*i+3]);
            REQUIRE(e->end_us > e->begin_us);
            if (i) REQUIRE(e->target_us > d->events[i-1].target_us);
        }
        REQUIRE(d->late == s->late && d->dropped == s->dropped && d->unapplied == s->unapplied);
        writes = 0; missing_h = 1;
        REQUIRE(!pc98_splash_raster_table(s, &logo));
        REQUIRE(d->late && d->unapplied && !d->events[1].applied);
        REQUIRE(d->if0_total_us > d->if0_max_us);
        pc98_splash_raster_reset();
        REQUIRE(!d->count && !d->if0_total_us && !d->late);
        setup(mode, 0); s = pc98_splash_raster_state();
        REQUIRE(!pc98_splash_raster_frame(s, &fixed));
        REQUIRE(d->count == 16 && !d->if0_total_us);
        for (u32 i = 0; i < 16; i++) REQUIRE(d->events[i].applied && d->events[i].index == i);
    }
    finish(0);
}
