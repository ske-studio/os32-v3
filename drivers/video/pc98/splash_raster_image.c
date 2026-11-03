/* Image-only palette columns and continuous H counting. The public/logo
 * player keeps its own short IF windows. I/O/phase references are the same
 * NEC/BSD references as splash_raster.c; timing contract: PL_DESIGN section 3.
 * R1a/R1b must certify this exact machine code, including count=0 and every
 * adjacent count transition. No M1 measurement is an adopted R1 bound. */
#include "splash_raster_image.h"
#include "gfx_osri.h"
#include "palette.h"
#include "pc98.h"
#include "io.h"
#include "sys.h"
#include "ktime.h"
#include "time_splash_math.h"
#include "pit_math.h"
#include "idt.h"

/* Change only for an explicit N6 diagnostic candidate, never from the file. */
#define PL_ADOPTED_OUTS SPLASH_RASTER_IMAGE_OUTS
#define PL_INITIAL_OUTS (OSRI_COLORS * 4u)
#define PL_IF_MAX_US 20000u

typedef unsigned long long PlTime;
typedef struct SplashRasterTimeRange PlRange;

/* Every time-bearing element has its own denominator. An unset element is
 * distinct from a measured zero duration (notably W for an empty column). */
struct PlCost { u32 den; PlRange time; };
struct PlWindow {
    u32 den, lower, upper;
    /* Time to the ith IN return = base + (i-1)*step. Bounds include
     * branches, the preceding column, next-row preparation and first index.
     * The normal windows start at the PREVIOUS successful IN return.
     * First starts at the post-CLI clock (VS), or captured F (whole
     * blank); capture_f starts at that post-CLI clock. Their base includes
     * clock return, entry checks, row/branch selection and first IN. */
    PlRange base, step;
};
struct PlCountBudget {
    struct SplashRasterImageBudget io;
    struct PlCost prepare;
    struct PlWindow edge;
};
enum PlExtra {
    PL_OPEN_POLL,           /* IF=1 poll, clock and permitted IRQ included */
    PL_CLOCK_ERROR,         /* error of brackets around an IN */
    PL_C64,
    PL_CLI_HEAD,            /* post-CLI clock -> entry checks -> first IN */
    PL_RECONFIRM,           /* last legal A/G entry -> first IN */
    PL_FIRST_PHASE,         /* F -> first H, fixed by the phase convention */
    PL_FRAME_REMAIN,        /* CLI -> visible end; Cexit excluded */
    PL_COMMON_EXIT,         /* free, both clears, shutdown, default/text */
    PL_CEXIT,               /* all exit sampling/retries/settle/IF/IRQ0 */
    PL_LATCH_CLOCK,         /* entry PIT latch -> post-CLI clock (or failure) */
    PL_CLI_CLOCK,           /* irq_save -> entry sample -> post-CLI clock */
    PL_EXIT_HEAD,           /* final IN/failed entry check -> settle label */
    PL_EXTRA_COUNT
};
enum PlExit { PL_EXIT_OK, PL_EXIT_DROP, PL_EXIT_SYNC, PL_EXIT_DEADLINE,
              PL_EXIT_COUNT };
struct PlProfile {
    u8 n;
    struct PlCountBudget count[PL_MAX_OUTS + 1];
    struct PlCost extra[PL_EXTRA_COUNT];
    struct PlCost remaining[PL_INITIAL_OUTS]; /* before each initial OUT */
    struct PlWindow capture_f, first, end_low;
    /* Common settle label -> adopted exit latch, for each path. Includes
     * arithmetic, the sample call and all rejected IRR/latch/IRR attempts.
     * Added to the actual prefix's independent iteration bounds, never
     * inferred from PIT differences. Cexit also covers settle's re-sample. */
    struct PlCost exit[PL_EXIT_COUNT];
};

/* Ra266 only. [24/31k][whole blank/VS][N5/N6]. R1 deliberately supplies
 * NO costs, windows or denominators. All members must be installed together
 * by R1a, then replaced/re-certified by R1b/M2. No runtime CPU guessing.
 * N6 slots are dormant unless the diagnostic candidate explicitly adopts 6. */
#ifndef PL_PROFILES_CONST
#define PL_PROFILES_CONST const
#endif
static PL_PROFILES_CONST struct PlProfile pl_profiles[2][2][2] = {
    { { { .n = 5 }, { .n = 6 } }, { { .n = 5 }, { .n = 6 } } },
    { { { .n = 5 }, { .n = 6 } }, { { .n = 5 }, { .n = 6 } } }
};

/* One boot image at a time (cooperative, single-task caller). Preparation
 * binds this context to that image/plan; no edge state belongs to the logo
 * player. Carry the F interval across frame calls so that acquiring an
 * anchor does not insert unplayed visible frames between successful ones.
 * A missed capture advances the interval, never fabricates an observed F. */
static struct {
    const struct SplashRaster *state;
    const struct OsriImage *image;
    const struct PlPlan *plan;
    PlTime fmin, fmax, last_clock;
    int anchored, clocked;
} pl_context;

static PlRange window_time(const struct PlWindow *w, u32 i)
{
    return (PlRange){ w->base.min + (long long)(i - 1) * w->step.min,
                      w->base.max + (long long)(i - 1) * w->step.max };
}

static int range_valid(PlRange r)
{
    return r.min >= 0 && r.min <= r.max;
}

static int cost_valid(const struct PlCost *c, u32 den)
{
    return c->den == den && range_valid(c->time);
}

static int window_valid(const struct PlWindow *w, u32 den)
{
    return w->den == den && w->lower > 0 && w->lower < w->upper &&
           range_valid(w->base) && range_valid(w->step) && w->step.min > 0;
}

static const struct PlProfile *profile(const struct SplashRaster *s, u8 n)
{
    if (!s || !s->checked || !s->dynamic_ok ||
        s->mode > SPLASH_RASTER_31K || n != PL_ADOPTED_OUTS ||
        n < SPLASH_RASTER_IMAGE_OUTS || n > PL_MAX_OUTS ||
        (s->active0_lines && s->active0_lines != s->vbp_lines)) return NULL;
    return &pl_profiles[s->mode][!!s->active0_lines]
                       [n - SPLASH_RASTER_IMAGE_OUTS];
}

/* First check EVERY denominator, before any division or column preparation.
 * This intentionally rejects a partially installed R1a/R1b table as well. */
static int qualified(const struct PlProfile *p, const struct SplashRaster *s)
{
    if (!p || p->n != PL_ADOPTED_OUTS) return 0;
    u32 den = p->count[0].io.den;
    if (!den) return 0;
    for (u32 i = 0; i <= p->n; i++) {
        const struct PlCountBudget *c = &p->count[i];
        const struct SplashRasterImageBudget *b = &c->io;
        if (b->den != den || b->outs != i || !range_valid(b->q) ||
            !range_valid(b->d) || !range_valid(b->w) ||
            !range_valid(b->h) || !range_valid(b->b) || b->error < 0 ||
            !cost_valid(&c->prepare, den) || !window_valid(&c->edge, den))
            return 0;
    }
    for (u32 i = 0; i < PL_EXTRA_COUNT; i++)
        if (!cost_valid(&p->extra[i], den)) return 0;
    for (u32 i = 0; i < PL_INITIAL_OUTS; i++)
        if (!cost_valid(&p->remaining[i], den)) return 0;
    for (u32 i = 0; i < PL_EXIT_COUNT; i++)
        if (!cost_valid(&p->exit[i], den)) return 0;
    if (!window_valid(&p->capture_f, den) ||
        !window_valid(&p->first, den) || !window_valid(&p->end_low, den))
        return 0;

    for (u32 i = 0; i <= p->n; i++) {
        const struct PlCountBudget *c = &p->count[i];
        const struct SplashRasterImageBudget *b = &c->io;
        PlRange limit = window_time(&c->edge, c->edge.upper);
        PlRange lower = window_time(&c->edge, c->edge.lower);
        /* Check every count, including empty rows and index preparation.
         * Upper is tested before accepting a sample, so its SHORTEST time
         * must exceed the latest normal edge. Its longest time must stop
         * before the earliest edge following a single missing pulse. */
        if (b->h.min <= b->b.max || b->b.min <= 0 ||
            b->h.min < p->count[0].io.h.min ||
            b->h.max > p->count[0].io.h.max ||
            b->b.max > p->count[0].io.b.max ||
            b->q.max + b->d.max + b->w.max + b->error >= b->b.min ||
            b->q.max >= b->b.min || b->q.max >= b->h.min - b->b.max ||
            c->prepare.time.max >= b->h.min - b->b.max ||
            b->h.max + b->q.max + b->error >= limit.min ||
            limit.max >= 2 * b->h.min - b->q.max - b->error ||
            lower.min < b->h.min - b->q.max - b->error ||
            (c->edge.lower > 1 &&
             window_time(&c->edge, c->edge.lower - 1).max >=
                 b->h.min - b->q.max - b->error) ||
            lower.max > b->h.min ||
            (SPLASH_RASTER_VISIBLE_LINES + 2) * b->h.max +
                p->extra[PL_CEXIT].time.max >= (long long)PL_IF_MAX_US * den ||
            p->extra[PL_FRAME_REMAIN].time.max <
                (SPLASH_RASTER_VISIBLE_LINES + 2) * b->h.max ||
            p->extra[PL_FRAME_REMAIN].time.max +
                p->extra[PL_CEXIT].time.max >= (long long)PL_IF_MAX_US * den ||
            p->extra[PL_CEXIT].time.max >=
                (s->total_lines - SPLASH_RASTER_VISIBLE_LINES) * b->h.min)
            return 0;
    }
    if (p->extra[PL_OPEN_POLL].time.min <= 0 ||
        p->extra[PL_FIRST_PHASE].time.max >= p->count[0].io.h.min ||
        p->extra[PL_C64].time.max < p->remaining[0].time.max)
        return 0;
    /* The PIT elapsed interval starts at its latch, but the IF contract
     * starts at irq_save. Include sampling and the post-CLI clock BEFORE
     * accepting the first edge window, including the whole-blank F wait. */
    if (p->extra[PL_CLI_CLOCK].time.max +
            (s->active0_lines ? 0 :
             window_time(&p->capture_f, p->capture_f.upper).max) +
            window_time(&p->first, p->first.upper).max >
                2 * p->count[0].io.h.min)
        return 0;
    /* The first edge after F has its own phase, not a nominal full H.
     * In VS mode first instead spans the selected common-low entry -> E(0)
     * and is checked against its absolute A interval for every frame. */
    if (!s->active0_lines) {
        const struct SplashRasterImageBudget *b = &p->count[0].io;
        PlRange lo = window_time(&p->first, p->first.lower);
        PlRange hi = window_time(&p->first, p->first.upper);
        if (lo.min < p->extra[PL_FIRST_PHASE].time.min - b->q.max - b->error ||
            (p->first.lower > 1 &&
             window_time(&p->first, p->first.lower - 1).max >=
                 p->extra[PL_FIRST_PHASE].time.min - b->q.max - b->error) ||
            hi.min <= p->extra[PL_FIRST_PHASE].time.max + b->q.max + b->error ||
            hi.max >= p->extra[PL_FIRST_PHASE].time.min + b->h.min -
                      b->q.max - b->error) return 0;
    }
    if (window_time(&p->end_low, p->end_low.upper).min <=
            p->count[0].io.b.max + p->count[0].io.q.max +
            p->count[0].io.error ||
        window_time(&p->end_low, p->end_low.upper).max >=
        p->count[0].io.h.min) return 0;
    return 1;
}

static int append(struct PlRow *r, u8 n, u8 port, u8 value)
{
    if (r->count == n) return -1;
    r->port[r->count] = port;
    r->value[r->count++] = value;
    return 0;
}

/* Returns 0 on success, -1 on failure. The loader has checked the format;
 * re-evaluate actual component costs for BOTH versions before commit. */
int pl_prepare(const struct OsriImage *image, u8 n, struct PlPlan *plan)
{
    struct SplashRaster *s = pc98_splash_raster_state();
    const struct PlProfile *p = profile(s, n);
    u16 palette[OSRI_COLORS];
    if (!image || !plan || !qualified(p, s)) return -1;
    pl_context.anchored = 0;
    pl_context.clocked = 0;
    pl_context.plan = NULL;
    for (u32 i = 0; i < OSRI_COLORS; i++) palette[i] = image->pal[i];
    for (u32 y = 0; y < SPLASH_RASTER_VISIBLE_LINES; y++)
        plan->row[y] = (struct PlRow){0};
    for (u32 i = 0; i < image->count; i++) {
        const struct OsriEvent *e = &image->event[i];
        if (!e->line || e->line >= SPLASH_RASTER_VISIBLE_LINES ||
            e->index >= OSRI_COLORS) return -1;
        struct PlRow *r = &plan->row[e->line];
        u16 old = palette[e->index], rgb = e->rgb444;
        if (!r->count) r->first = e->index;
        else if (append(r, n, PAL_IDX_PORT, e->index)) return -1;
        if (((old ^ rgb) & 0x0f0) &&
            append(r, n, PAL_G_PORT, (rgb >> 4) & GFX_PALETTE_COMPONENT_MASK))
            return -1;
        if (((old ^ rgb) & 0xf00) &&
            append(r, n, PAL_R_PORT, (rgb >> 8) & GFX_PALETTE_COMPONENT_MASK))
            return -1;
        if (((old ^ rgb) & 0x00f) &&
            append(r, n, PAL_B_PORT, rgb & GFX_PALETTE_COMPONENT_MASK))
            return -1;
        palette[e->index] = rgb;
    }
    pl_context.state = s;
    pl_context.image = image;
    pl_context.plan = plan;
    return 0;
}

static int now_us(PlTime *t)
{
    u32 lo, hi;
    if (sys_time_now(&lo, &hi)) return -1;
    *t = ((PlTime)hi << BITS_PER_U32) | lo;
    if (pl_context.clocked && *t < pl_context.last_clock) return -1;
    pl_context.last_clock = *t;
    pl_context.clocked = 1;
    return 0;
}

static PlTime ceil_us(long long t, u32 den)
{
    return ((PlTime)t + den - 1) / den;
}

static PlTime earlier(PlTime a, PlTime b) { return a < b ? a : b; }

/* Open waits have a real deadline AND a calibrated stopped-clock bound.
 * Unlike the continuous scan these may read the clock. Bracket every IN,
 * including interrupt delays, rather than stamping a delayed V rise as F. */
static int open_status(const struct PlProfile *p, u32 bit, int high,
                       PlTime deadline, PlTime *before, PlTime *after)
{
    PlTime start, prev, end;
    u32 den = p->count[0].io.den;
    if (now_us(&start) || start > deadline) return -1;
    PlTime limit = (deadline - start) * den /
                   p->extra[PL_OPEN_POLL].time.min + 1;
    prev = start;
    for (PlTime i = 0; i < limit; i++) {
        if (now_us(before) || *before < prev) return -1;
        u32 status = inp(GDC_GFX_STAT);
        if (now_us(&end) || end < *before || end > deadline) return -1;
        if (!!(status & bit) == high) { *after = end; return 0; }
        prev = end;
    }
    return -1;
}

static int open_until(const struct PlProfile *p, PlTime target, PlTime *now)
{
    u32 den = p->count[0].io.den;
    PlTime prev;
    if (now_us(&prev)) return -1;
    if (prev >= target) { *now = prev; return 0; }
    PlTime limit = (target - prev) * den /
                   p->extra[PL_OPEN_POLL].time.min + 1;
    for (PlTime i = 0; i < limit; i++) {
        if (now_us(now) || *now < prev) return -1;
        if (*now >= target) return 0;
        prev = *now;
    }
    return -1;
}

/* Always acquire a fresh F interval. Preserve the last observed HIGH's
 * pre-IN time, since an IRQ may span F before the first LOW is sampled. */
static int fresh_f(const struct PlProfile *p, PlTime deadline, int new_cycle,
                   PlTime *fmin, PlTime *fmax)
{
    PlTime before, after, prev;
    if (new_cycle &&
        open_status(p, GDC_STAT_VSYNC, 0, deadline, &before, &after)) return -1;
    if (open_status(p, GDC_STAT_VSYNC, 1, deadline, &before, &after)) return -1;
    *fmin = before;
    prev = after;
    u32 den = p->count[0].io.den;
    PlTime limit = (deadline - after) * den /
                   p->extra[PL_OPEN_POLL].time.min + 1;
    for (PlTime i = 0; i < limit; i++) {
        if (now_us(&before) || before < prev) return -1;
        u32 status = inp(GDC_GFX_STAT);
        if (now_us(&after) || after < before || after > deadline) return -1;
        if (!(status & GDC_STAT_VSYNC)) {
            PlTime error = ceil_us(p->extra[PL_CLOCK_ERROR].time.max, den);
            *fmin = *fmin > error ? *fmin - error : 0;
            *fmax = after + error;
            return 0;
        }
        *fmin = before;
        prev = after;
    }
    return -1;
}

/* Bounds of numbered H edge k (A), from the fresh F interval. */
static void edge_interval(const struct PlProfile *p, PlTime fmin, PlTime fmax,
                          u32 k, PlTime *lo, PlTime *hi)
{
    u32 den = p->count[0].io.den;
    const PlRange h = p->count[0].io.h;
    PlRange phase = p->extra[PL_FIRST_PHASE].time;
    *lo = fmin + (phase.min + (long long)(k - 1) * h.min) / den;
    *hi = fmax + ceil_us(phase.max + (long long)(k - 1) * h.max, den);
}

/* Initial palette, entirely outside the continuous protected scan. Every
 * OUT is checked against the remaining measured column (clock/IRQ included).
 * A partly written palette is discarded; next frame restores all 16 colors. */
static int initial_palette(const struct PlProfile *p,
                           const struct OsriImage *image, PlTime linit)
{
    u32 den = p->count[0].io.den, j = 0;
    PlTime now, prev;
    if (now_us(&now)) return PL_FRAME_NO_SYNC;
    PlTime error = ceil_us(p->extra[PL_CLOCK_ERROR].time.max, den);
    if (now + ceil_us(p->extra[PL_C64].time.max, den) + error > linit)
        return PL_FRAME_DROP;
    prev = now;
    for (u32 i = 0; i < OSRI_COLORS; i++) {
        u16 rgb = image->pal[i];
        const u8 port[] = { PAL_IDX_PORT, PAL_G_PORT, PAL_R_PORT, PAL_B_PORT };
        const u8 value[] = { i, (rgb >> 4) & GFX_PALETTE_COMPONENT_MASK,
                            (rgb >> 8) & GFX_PALETTE_COMPONENT_MASK,
                            rgb & GFX_PALETTE_COMPONENT_MASK };
        for (u32 c = 0; c < 4; c++, j++) {
            if (now_us(&now) || now < prev) return PL_FRAME_NO_SYNC;
            if (now + ceil_us(p->remaining[j].time.max, den) + error > linit)
                return PL_FRAME_DROP;
            outp(port[c], value[c]);
            prev = now;
        }
    }
    if (now_us(&now) || now < prev) return PL_FRAME_NO_SYNC;
    return now + error <= linit ? PL_FRAME_OK : PL_FRAME_DROP;
}

/* Count-specific expanded RAM port/value columns, same shape as M1. The
 * branch/function selection is done BEFORE the edge, never a mask decoder
 * or a count loop inside H. D includes the selected indirect call. */
typedef void (*PlEmit)(const struct PlRow *);
#define PL_OUT(i) outp(r->port[i], r->value[i])
static void emit0(const struct PlRow *r) { (void)r; }
static void emit1(const struct PlRow *r) { PL_OUT(0); }
static void emit2(const struct PlRow *r) { PL_OUT(0); PL_OUT(1); }
static void emit3(const struct PlRow *r) { PL_OUT(0); PL_OUT(1); PL_OUT(2); }
static void emit4(const struct PlRow *r) { PL_OUT(0); PL_OUT(1); PL_OUT(2); PL_OUT(3); }
static void emit5(const struct PlRow *r) { PL_OUT(0); PL_OUT(1); PL_OUT(2); PL_OUT(3); PL_OUT(4); }
static void emit6(const struct PlRow *r) { PL_OUT(0); PL_OUT(1); PL_OUT(2); PL_OUT(3); PL_OUT(4); PL_OUT(5); }
#undef PL_OUT
static const PlEmit emitters[] = { emit0, emit1, emit2, emit3, emit4, emit5, emit6 };

/* One continuous edge window: low wait AND high wait consume the SAME
 * budget. Upper and lower checks precede the edge count and every RGB OUT.
 * Row/index selection, rearming and all adjacent count transitions are in Q.
 * No clock, color expansion, event search or diagnostics in this loop. */
static int next_edge(const struct PlWindow *w, const struct PlRow *r,
                     PlEmit emit, int require_low, u32 *iterations)
{
    int armed = 0;
    for (u32 i = 1; ; i++) {
        u32 status = inp(GDC_GFX_STAT);
        if (i >= w->upper) { *iterations = i; return -1; }
        if (i == 1 && require_low && (status & GDC_STAT_HBLANK)) {
            *iterations = i;
            return -1;
        }
        if (!(status & GDC_STAT_HBLANK)) {
            if (!armed) {
                armed = 1;
                if (r->count) outp(PAL_IDX_PORT, r->first);
            }
        } else if (armed) {
            *iterations = i;
            if (i < w->lower) return -1;
            emit(r);
            return 0;
        }
    }
}

static int capture_f(const struct PlWindow *w, u32 *iterations)
{
    u32 previous = 0;
    for (u32 i = 1; ; i++) {
        u32 status = inp(GDC_GFX_STAT);
        *iterations = i;
        if (i >= w->upper) return -1;
        if (i == 1) {
            if (!(status & GDC_STAT_VSYNC)) return -1;
            previous = status;
            continue;
        }
        if (!(status & GDC_STAT_VSYNC)) {
            if (i < w->lower ||
                ((previous ^ status) & GDC_STAT_HBLANK)) return -1;
            return 0; /* no new H inferred from this F sample */
        }
        previous = status;
    }
}

/* E(400) is before the visible-end boundary. Keep IF=0 through its falling
 * edge too; a successful rising-edge count alone does not end protection. */
static int visible_end(const struct PlWindow *w, u32 *iterations)
{
    for (u32 i = 1; ; i++) {
        u32 status = inp(GDC_GFX_STAT);
        *iterations = i;
        if (i >= w->upper) return -1;
        if (!(status & GDC_STAT_HBLANK)) return i < w->lower ? -1 : 0;
    }
}

static void add_window(PlRange *elapsed, const struct PlWindow *w, u32 i)
{
    PlRange span = window_time(w, i);
    elapsed->min += span.min;
    elapsed->max += span.max;
}

static int finish(struct SplashRaster *s, const struct OsriImage *image,
                  u32 applied_line, int rc)
{
    if (rc == PL_FRAME_DROP) {
        s->dropped++;
        for (u32 i = 0; i < image->count; i++)
            if (image->event[i].line > applied_line) {
                s->late++;
                s->unapplied++;
            }
    }
    s->last_result = rc;
    return rc;
}

int pl_frame(struct SplashRaster *s, const struct OsriImage *image,
             const struct PlPlan *plan, PlTime deadline_us)
{
    const struct PlProfile *p = profile(s, PL_ADOPTED_OUTS);
    const struct pit_setup *pit = pit_get_setup();
    struct TimeSplashMathInput settle = {0};
    PlTime now, fmin, fmax, linit, enter_min, enter_max, visible_min, dummy;
    PlTime cutoff, reserve;
    u32 applied_line = 0, loops = 0, den;
    int rc, exit_kind, dropped = 0;
    const struct PlRow empty = {0};
    if (!s || !image || !plan || !qualified(p, s) ||
        (image->count && !_irq_enabled()) || !pit->valid ||
        pl_context.state != s || pl_context.image != image || pl_context.plan != plan)
        return PL_FRAME_NO_SYNC;
    den = p->count[0].io.den;
    if (now_us(&now)) return finish(s, image, 0, PL_FRAME_NO_SYNC);
    /* Account for fresh F acquisition (up to two frames), any extra wait
     * for the next whole-blank VBLANK, palette, full scan, PIT and cleanup.
     * VS acquisition has its separate 100ms cap; never restart boot time. */
    u32 wait_frames = !s->active0_lines && pl_context.anchored ? 1 : 3;
    reserve = ceil_us(wait_frames * (long long)s->total_lines * p->count[0].io.h.max +
                      p->extra[PL_C64].time.max +
                      p->extra[PL_FRAME_REMAIN].time.max +
                      p->extra[PL_CEXIT].time.max +
                      p->extra[PL_COMMON_EXIT].time.max, den);
    if (now >= deadline_us || reserve > deadline_us - now)
        return finish(s, image, 0, PL_FRAME_DEADLINE);
    cutoff = earlier(now + SPLASH_RASTER_SYNC_TIMEOUT_US,
                     deadline_us - reserve +
                     ceil_us(wait_frames * (long long)s->total_lines *
                             p->count[0].io.h.max, den));
    if (!s->active0_lines && pl_context.anchored) {
        fmin = pl_context.fmin;
        fmax = pl_context.fmax;
    } else if (fresh_f(p, cutoff, !pl_context.anchored, &fmin, &fmax)) {
        return finish(s, image, 0, PL_FRAME_NO_SYNC);
    }

    if (s->active0_lines) {
        /* VS: A(k) and G(k) recover the unobserved VBP edges. Start at
         * G(active0), so the first counted edge is E(0), not row 1. */
        PlTime prev_min, prev_max, edge_max;
        pl_context.fmin = fmin;
        pl_context.fmax = fmax;
        pl_context.anchored = 1;
        edge_interval(p, fmin, fmax, s->active0_lines - 1, &prev_min, &prev_max);
        edge_interval(p, fmin, fmax, s->active0_lines, &visible_min, &edge_max);
        enter_min = prev_max + ceil_us(p->count[0].io.b.max, den) + 1;
        PlTime head = ceil_us(p->extra[PL_CLI_HEAD].time.max +
                              p->extra[PL_RECONFIRM].time.max, den);
        if (visible_min <= head) return finish(s, image, 0, PL_FRAME_DROP);
        enter_max = visible_min - head - 1;
        linit = enter_max;
    } else {
        /* Whole-blank: this fresh F is only a prediction anchor. Initialize
         * at the NEXT V rise, then CLI before the earliest next F and
         * actually capture it. A delayed V rise never changes this anchor. */
        fmin += (long long)s->total_lines * p->count[0].io.h.min / den;
        fmax += ceil_us((long long)s->total_lines * p->count[0].io.h.max, den);
        /* Wait until the earliest possible NEW blank before accepting a
         * high V. A drop can return during the old blank; that old high
         * must not initialize the palette for the following frame. */
        PlTime blank_max = ceil_us((s->total_lines - SPLASH_RASTER_VISIBLE_LINES) *
                                   p->count[0].io.h.max, den);
        PlTime rise_min = fmin > blank_max ? fmin - blank_max : 0;
        if (open_until(p, rise_min, &now) ||
            open_status(p, GDC_STAT_VSYNC, 1, cutoff, &now, &dummy))
            return finish(s, image, 0, PL_FRAME_NO_SYNC);
        pl_context.fmin = fmin;
        pl_context.fmax = fmax;
        pl_context.anchored = 1;
        visible_min = fmin;
        PlTime head = ceil_us(p->extra[PL_CLI_HEAD].time.max, den);
        if (fmin <= head) return finish(s, image, 0, PL_FRAME_DROP);
        PlTime first_latest = fmax +
            ceil_us(p->extra[PL_FIRST_PHASE].time.max, den);
        PlTime two_h = 2 * p->count[0].io.h.min / den;
        enter_min = first_latest > two_h ? first_latest - two_h : 0;
        enter_max = fmin - head - 1;
        linit = enter_max;
    }
    PlTime tail = ceil_us(p->extra[PL_FRAME_REMAIN].time.max +
                          p->extra[PL_CEXIT].time.max +
                          p->extra[PL_COMMON_EXIT].time.max, den);
    linit = earlier(linit, earlier(visible_min, deadline_us - tail));
    if (enter_min > enter_max) {
        pl_context.anchored = 0; /* accumulated uncertainty: acquire anew */
        return finish(s, image, 0, PL_FRAME_DROP);
    }
    rc = initial_palette(p, image, linit);
    if (rc != PL_FRAME_OK) return finish(s, image, 0, rc);
    s->frame++;
    if (!image->count) return finish(s, image, 0, PL_FRAME_OK);
    PlTime error = ceil_us(p->extra[PL_CLOCK_ERROR].time.max, den);
    if (open_until(p, enter_min + error, &now))
        return finish(s, image, 0, PL_FRAME_NO_SYNC);
    /* Precheck before CLI; a second clock immediately after CLI rejects a
     * racing IRQ that moved us to another common-low interval. Neither
     * clock is in the continuous scan, and neither triggers RGB writes. */
    if (now + error > enter_max || now + error + tail > deadline_us)
        return finish(s, image, 0, PL_FRAME_DROP);
    PlTime first_min, first_max;
    edge_interval(p, fmin, fmax, s->active0_lines ? s->active0_lines : 1,
                  &first_min, &first_max);
    PlRange first_lo = window_time(&p->first, p->first.lower);
    PlRange first_hi = window_time(&p->first, p->first.upper);
    PlRange first_prev = p->first.lower > 1 ?
                         window_time(&p->first, p->first.lower - 1) : (PlRange){0};
    unsigned int flags = irq_save();
    PlRange elapsed = p->extra[PL_LATCH_CLOCK].time;
    rc = PL_FRAME_DROP;
    if (ktime_splash_sample(&settle.start)) { rc = PL_FRAME_DEADLINE; goto restore; }
    if (now_us(&now)) { rc = PL_FRAME_NO_SYNC; goto settle_exit; }
    if (now < enter_min + error || now + error > enter_max ||
        now + error + tail > deadline_us) goto settle_exit;
    if (s->active0_lines &&
        (now + first_lo.min / den < first_min + error ||
         (p->first.lower > 1 &&
          now + error + ceil_us(first_prev.max, den) >= first_min) ||
         now + first_hi.min / den <= first_max + error ||
         now + error + ceil_us(first_hi.max, den) >=
             first_min + p->count[0].io.h.min / den)) goto settle_exit;
    if (!s->active0_lines) {
        PlRange cap_lo = window_time(&p->capture_f, p->capture_f.lower);
        PlRange cap_hi = window_time(&p->capture_f, p->capture_f.upper);
        PlRange cap_prev = p->capture_f.lower > 1 ?
            window_time(&p->capture_f, p->capture_f.lower - 1) : (PlRange){0};
        if (now + cap_lo.min / den < fmin + error ||
            (p->capture_f.lower > 1 &&
             now + error + ceil_us(cap_prev.max, den) >= fmin) ||
            now + cap_hi.min / den <= fmax + error ||
            now + error + ceil_us(cap_hi.max, den) >=
                fmin + p->count[0].io.h.min / den ||
            first_max > now + 2 * p->count[0].io.h.min / den) goto settle_exit;
    }
    if (!s->active0_lines) {
        int captured = capture_f(&p->capture_f, &loops);
        add_window(&elapsed, &p->capture_f, loops);
        if (captured) goto settle_exit;
        /* Intersect prediction with actual HIGH/LOW sample brackets.
         * IN-return ranges are moved outward by Q to include IN itself.
         * This work is part of the first window's measured base, not H. */
        PlRange before = window_time(&p->capture_f, loops - 1);
        PlRange after = window_time(&p->capture_f, loops);
        long long lower = before.min - p->count[0].io.q.max;
        PlTime observed_min = now - error + (lower > 0 ? lower / den : 0);
        PlTime observed_max = now + error + ceil_us(after.max, den);
        if (pl_context.fmin < observed_min) pl_context.fmin = observed_min;
        if (pl_context.fmax > observed_max) pl_context.fmax = observed_max;
        if (pl_context.fmin > pl_context.fmax) {
            pl_context.anchored = 0;
            goto settle_exit;
        }
    }
    const struct PlRow *first = s->active0_lines ? &empty : &plan->row[1];
    PlEmit emit = emitters[first->count];
    int missed = next_edge(&p->first, first, emit, !!s->active0_lines, &loops);
    add_window(&elapsed, &p->first, loops);
    if (missed) goto settle_exit;
    if (!s->active0_lines) applied_line = 1;
    for (u32 y = s->active0_lines ? 1 : 2;
         y <= SPLASH_RASTER_VISIBLE_LINES; y++) {
        const struct PlRow *row = y == SPLASH_RASTER_VISIBLE_LINES ?
                                  &empty : &plan->row[y];
        /* Window starts at the previous IN: use the preceding column's
         * count. Each bound envelopes ALL next-row/count preparations. */
        const struct PlWindow *w = &p->count[first->count].edge;
        emit = emitters[row->count];
        missed = next_edge(w, row, emit, 0, &loops);
        add_window(&elapsed, w, loops);
        if (missed) goto settle_exit;
        applied_line = y;
        first = row;
    }
    missed = visible_end(&p->end_low, &loops);
    add_window(&elapsed, &p->end_low, loops);
    if (missed) goto settle_exit;
    rc = PL_FRAME_OK;

settle_exit:
    dropped = rc == PL_FRAME_DROP;
    exit_kind = rc == PL_FRAME_OK ? PL_EXIT_OK :
                rc == PL_FRAME_NO_SYNC ? PL_EXIT_SYNC :
                rc == PL_FRAME_DEADLINE ? PL_EXIT_DEADLINE : PL_EXIT_DROP;
    /* Include the failed window too, including low wait, early edges and
     * the final upper-bound IN. The exit's retry cost is independent of the
     * PIT count; its phase never determines the elapsed-range estimate. */
    elapsed.min += p->extra[PL_EXIT_HEAD].time.min + p->exit[exit_kind].time.min;
    elapsed.max += p->extra[PL_EXIT_HEAD].time.max + p->exit[exit_kind].time.max;
    settle.reload = pit->reload;
    /* Outward rounding to PIT input clocks, not reload periods. */
    settle.elapsed_min = (PlTime)elapsed.min * pit->clk_hz /
                         ((PlTime)den * USEC_PER_SEC);
    settle.elapsed_max = ((PlTime)elapsed.max * pit->clk_hz +
                         (PlTime)den * USEC_PER_SEC - 1) /
                         ((PlTime)den * USEC_PER_SEC);
    if (ktime_splash_sample(&settle.end)) rc = PL_FRAME_DEADLINE;
    /* Call even if exit sampling failed to consume the pair. Failure (also
     * the P2 B=0 state) means common exit, never a guessed tick or new frame. */
    if (ktime_splash_settle(&settle)) rc = PL_FRAME_DEADLINE;
restore:
    irq_restore(flags);
    if (dropped && rc != PL_FRAME_DROP)
        finish(s, image, applied_line, PL_FRAME_DROP);
    return finish(s, image, applied_line, rc);
}
