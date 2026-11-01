/* Real player with diagnostics; the original suite still runs without them. */
#define _start raster_original_start
#include "splash_raster_host.c"
#undef _start

void _start(void)
{
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
