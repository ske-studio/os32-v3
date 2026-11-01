/* Shared PC98 palette player. No framebuffer, filesystem or relative delays.
 * I/O reference: FreeBSD releng/10.4 sys/pc98/cbus/gdc.c:655,685,831
 * (BSD-3-Clause); hardware/phase derivation: RASTER_SPLASH_DESIGN sections 2/5. */
#include "splash_raster.h"
#include "gfx_osri.h"
#include "palette.h"
#include "io.h"
#include "pc98.h"
#include "pegc.h"
#include "sys.h"

#define MEASURE_PERIODS 8u
#define POLL_LIMIT 1000000u
#define PERCENT 100u
#define PERIOD_TOLERANCE 1u

static struct SplashRaster raster;
static const u8 sync_24[] = PEGC_GDC_MSYNC_400;
static const u8 sync_31[] = PEGC_GDC_MSYNC_400_31K;

static int result(struct SplashRaster *s, int rc)
{
    s->last_result = rc;
    return rc;
}

static int now_us(unsigned long long *t)
{
    u32 lo, hi;
    if (sys_time_now(&lo, &hi)) return SPLASH_RASTER_NO_SYNC;
    *t = ((unsigned long long)hi << BITS_PER_U32) | lo;
    return 0;
}

static u32 lines_us(const struct SplashRaster *s, u32 lines)
{
    return (unsigned long long)lines * s->line_num_us / s->line_den;
}

/* All waits have both a clock deadline and a finite stopped-clock bound. */
static int wait_bit(u32 bit, int high, unsigned long long deadline,
                    unsigned long long *stamp)
{
    unsigned long long prev;
    if (now_us(&prev)) return SPLASH_RASTER_NO_SYNC;
    for (u32 poll = 0; poll < POLL_LIMIT; poll++) {
        u32 status = inp(GDC_GFX_STAT);
        if (now_us(stamp) || *stamp < prev || *stamp > deadline ||
            (*stamp == deadline && !!(status & bit) != high))
            return SPLASH_RASTER_NO_SYNC;
        if (!!(status & bit) == high) return 0;
        prev = *stamp;
    }
    return SPLASH_RASTER_NO_SYNC;
}

static int wait_until(unsigned long long target, unsigned long long *stamp)
{
    unsigned long long prev;
    if (now_us(&prev)) return SPLASH_RASTER_NO_SYNC;
    for (u32 poll = 0; poll < POLL_LIMIT; poll++) {
        if (now_us(stamp) || *stamp < prev) return SPLASH_RASTER_NO_SYNC;
        if (*stamp >= target) return 0;
        prev = *stamp;
    }
    return SPLASH_RASTER_NO_SYNC;
}

static int new_vsync(unsigned long long *rise)
{
    unsigned long long t, deadline;
    if (now_us(&t)) return SPLASH_RASTER_NO_SYNC;
    deadline = t + SPLASH_RASTER_SYNC_TIMEOUT_US;
    if (wait_bit(GDC_STAT_VSYNC, 0, deadline, &t)) return SPLASH_RASTER_NO_SYNC;
    return wait_bit(GDC_STAT_VSYNC, 1, deadline, rise);
}

void pc98_splash_raster_reset(void)
{
    raster = (struct SplashRaster){0};
}

struct SplashRaster *pc98_splash_raster_state(void)
{
    if (!raster.checked) pc98_splash_raster_begin(&raster);
    return &raster;
}

int pc98_splash_raster_begin(struct SplashRaster *s)
{
    unsigned long long first, rise, fall, high = 0, measured, expected, delta;
    const u8 *sync;
    if (s->checked) return s->last_result;
    s->checked = 1;
    s->dynamic_ok = 0;
    s->mode = (inp(PEGC_HSYNC_PORT) & PEGC_HSYNC_READ_HF) ?
              SPLASH_RASTER_31K : SPLASH_RASTER_24K;
    s->line_num_us = SPLASH_RASTER_LINE_NUM_US;
    s->line_den = s->mode == SPLASH_RASTER_31K ?
                  SPLASH_RASTER_31K_DEN : SPLASH_RASTER_24K_DEN;
    sync = s->mode == SPLASH_RASTER_31K ? sync_31 : sync_24;
    s->vs_lines = (sync[2] >> 5) | ((sync[3] & 3) << 3);
    s->vbp_lines = sync[7] >> 2;
    s->total_lines = sync[6] + ((sync[7] & 3) << BITS_PER_BYTE) +
                     sync[5] + s->vs_lines + s->vbp_lines;
    if (!_irq_enabled() || new_vsync(&first))
        return result(s, SPLASH_RASTER_NO_SYNC);
    rise = first;
    for (u32 i = 0; i < MEASURE_PERIODS; i++) {
        if (wait_bit(GDC_STAT_VSYNC, 0, rise + SPLASH_RASTER_SYNC_TIMEOUT_US, &fall))
            return result(s, SPLASH_RASTER_NO_SYNC);
        high += fall - rise;
        if (new_vsync(&rise)) return result(s, SPLASH_RASTER_NO_SYNC);
    }
    measured = (rise - first) * s->line_den;
    expected = (unsigned long long)MEASURE_PERIODS * s->total_lines * s->line_num_us;
    delta = measured > expected ? measured - expected : expected - measured;
    if (delta * PERCENT > expected * PERIOD_TOLERANCE)
        return result(s, SPLASH_RASTER_OK);
    s->high_us = (high + MEASURE_PERIODS - 1) / MEASURE_PERIODS;
    measured = high * s->line_den;
    expected = (unsigned long long)MEASURE_PERIODS *
               (s->total_lines - SPLASH_RASTER_VISIBLE_LINES) * s->line_num_us;
    delta = measured > expected ? measured - expected : expected - measured;
    if (delta <= (unsigned long long)MEASURE_PERIODS * s->line_num_us) {
        s->active0_lines = 0;
    } else {
        expected = (unsigned long long)MEASURE_PERIODS * s->vs_lines * s->line_num_us;
        delta = measured > expected ? measured - expected : expected - measured;
        if (delta > (unsigned long long)MEASURE_PERIODS * s->line_num_us)
            return result(s, SPLASH_RASTER_OK);
        s->active0_lines = s->vbp_lines;
    }
    s->dynamic_ok = 1;
    return result(s, SPLASH_RASTER_OK);
}

static void color(u8 index, u8 r, u8 g, u8 b)
{
    outp(PAL_IDX_PORT, index);
    outp(PAL_G_PORT, g & GFX_PALETTE_COMPONENT_MASK);
    outp(PAL_R_PORT, r & GFX_PALETTE_COMPONENT_MASK);
    outp(PAL_B_PORT, b & GFX_PALETTE_COMPONENT_MASK);
}

int pc98_splash_raster_frame(struct SplashRaster *s, const struct OsriImage *image)
{
    unsigned long long rise;
    if (!image || image->count || new_vsync(&rise))
        return result(s, SPLASH_RASTER_NO_SYNC);
    /* Fixed palette is independent of dynamic mode/phase qualification. */
    for (u32 i = 0; i < OSRI_COLORS; i++) {
        u16 rgb = image->pal[i];
        color(i, rgb >> 8, rgb >> 4, rgb);
    }
    s->frame++;
    return result(s, SPLASH_RASTER_OK);
}

/* Capture only the last line of the predicted high interval. A missed edge
 * discards this frame; it does not invalidate the cached timing profile. */
static int capture_fall(struct SplashRaster *s, unsigned long long rise,
                        unsigned long long *fall, unsigned long long *open_until)
{
    unsigned long long t, predicted = rise + s->high_us;
    u32 line = lines_us(s, 1);
    unsigned int flags;
    if (wait_until(predicted - line, &t)) return SPLASH_RASTER_NO_SYNC;
    if (t > predicted && (t - predicted) * 2 * s->line_den >= s->line_num_us)
        goto drop;
    flags = irq_save();
    int rc = wait_bit(GDC_STAT_VSYNC, 0, t + line, fall);
    irq_restore(flags);
    if (now_us(&t)) return SPLASH_RASTER_NO_SYNC;
    *open_until = t + (s->line_num_us + s->line_den - 1) / s->line_den;
    if (rc || (*fall > predicted &&
               (*fall - predicted) * 2 * s->line_den >= s->line_num_us)) goto drop;
    return 0;
drop:
    s->dropped++;
    return 1;
}

int pc98_splash_raster_table(struct SplashRaster *s, const GFX_RasterPalTable *t)
{
    unsigned long long rise, fall, now, open_until;
    u8 handled[GFX_RASTER_MAX_ENTRIES] = {0};
    u8 counted[GFX_RASTER_MAX_ENTRIES] = {0};
    int first = 0;
    if (!_irq_enabled() || !s->dynamic_ok || !t || t->count <= 0 ||
        t->count > GFX_RASTER_MAX_ENTRIES) return result(s, SPLASH_RASTER_NO_SYNC);
    for (int i = 0; i < t->count; i++) {
        const GFX_RasterPalEntry *e = &t->entries[i];
        if (e->line >= SPLASH_RASTER_VISIBLE_LINES || e->pal_idx >= OSRI_COLORS ||
            (i && e->line < t->entries[i-1].line + SPLASH_RASTER_MIN_GAP))
            return result(s, SPLASH_RASTER_NO_SYNC);
    }
    if (new_vsync(&rise)) return result(s, SPLASH_RASTER_NO_SYNC);
    if (t->entries[0].line == 0) {
        const GFX_RasterPalEntry *e = &t->entries[0];
        color(e->pal_idx, e->r, e->g, e->b);
        first = 1;
    }
    int capture = capture_fall(s, rise, &fall, &open_until);
    if (capture < 0) return result(s, SPLASH_RASTER_NO_SYNC);
    s->frame++;
    if (capture) { s->unapplied += t->count - first; return result(s, 0); }
    for (int i = first; i < t->count; i++) {
        if (handled[i]) continue;
        const GFX_RasterPalEntry *e = &t->entries[i];
        unsigned long long target = fall + lines_us(s, s->active0_lines + e->line);
        unsigned long long start = target - lines_us(s, 1);
        unsigned long long visible_end = fall + lines_us(s, s->active0_lines + SPLASH_RASTER_VISIBLE_LINES);
        int late = counted[i], applied = 0;
        if (start < open_until) start = open_until;
        if (wait_until(start, &now)) return result(s, SPLASH_RASTER_NO_SYNC);
        while (now < visible_end) {
            int selected = i;
            /* Coalesce an overdue index to its latest due value. */
            for (int j = i + 1; j < t->count; j++) {
                const GFX_RasterPalEntry *next = &t->entries[j];
                if (fall + lines_us(s, s->active0_lines + next->line) > now) break;
                if (!handled[j] && next->pal_idx == e->pal_idx) {
                    if (!handled[selected]) { handled[selected] = 1; s->unapplied++; }
                    e = next;
                    selected = j;
                }
            }
            unsigned long long deadline = now + lines_us(s, 2);
            unsigned int flags = irq_save();
            int rc = wait_bit(GDC_STAT_HBLANK, 0, deadline, &now);
            if (!rc) rc = wait_bit(GDC_STAT_HBLANK, 1, deadline, &now);
            if (now >= target && !late) { s->late++; late = 1; counted[i] = 1; }
            if (!rc && now < visible_end) {
                color(e->pal_idx, e->r, e->g, e->b);
                handled[selected] = 1;
                applied = 1;
            }
            irq_restore(flags);
            if (now_us(&now)) return result(s, SPLASH_RASTER_NO_SYNC);
            for (int j = i + 1; j < t->count; j++) {
                if (fall + lines_us(s, s->active0_lines + t->entries[j].line) > now) break;
                if (!counted[j]) { counted[j] = 1; s->late++; }
            }
            open_until = now + (s->line_num_us + s->line_den - 1) / s->line_den;
            if (wait_until(open_until, &now)) return result(s, SPLASH_RASTER_NO_SYNC);
            if (applied) break;
        }
        if (!applied) {
            for (int j = i; j < t->count; j++) if (!handled[j]) s->unapplied++;
            break;
        }
    }
    return result(s, SPLASH_RASTER_OK);
}
