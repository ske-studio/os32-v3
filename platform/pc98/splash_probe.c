/* Diagnostic-only foreground scan measurements. No output inside a scan.
 * I/O: FreeBSD releng/10.4 sys/pc98/cbus/gdc.c:655,685,831 (BSD-3-Clause).
 * Timing: NEC Technical Databook Hardware (1993), p.350-351; design section 2. */
#ifdef SPLASH_RASTER_DIAG
#include "platform_splash.h"
#include "gfx_osri.h"
#include "splash_raster.h"
#include "gfx.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "palette.h"
#include "pc98.h"
#include "pegc.h"
#include "paging.h"
#include "io.h"
#include "sys.h"
#include "serial.h"
#include "serial_diag.h"
#include "bootinfo.h"
#include "build_id.h"
#include "idt.h"
#include "ktime.h"

#define PROBE_FRAMES 32u
#define PROBE_HPERIODS 64u
#define PROBE_POLLS 1000000u
#define PROBE_SAMPLES 128u
#define PROBE_SHIFT 4
#define PROBE_STEP 8
#define PROBE_BAND_LINES 4
#define PROBE_MARK 16
#define PROBE_BIG_MARK 32
#define PROBE_CENTER_X 96
#define PROBE_CENTER_W 320
#define PROBE_RULER_X 432
#define PROBE_RULER_W 16
#define PROBE_TILE_X 592
#define PROBE_BORDER 4
#define PROBE_RX_BYTES 4096u
#define PROBE_COST_BATCH 64u
#define PROBE_BURST 8u
#define PROBE_ENTRIES 99u
#define PROBE_COST_KINDS 3u
#define PROBE_Q 0u
#define PROBE_C 1u
#define PROBE_W 2u

struct probe_edge { unsigned long long rise, fall, text, text_fall; };
struct probe_frame {
    unsigned long long duration, closed_max, closed_total;
    u32 late, dropped, unapplied, tick;
};
static struct probe_edge probe_edges[PROBE_FRAMES + 1];
static struct probe_frame probe_frames[PROBE_FRAMES];
static unsigned long long probe_hperiod[PROBE_HPERIODS], probe_hwidth[PROBE_HPERIODS];
static u32 probe_hsamples[PROBE_HPERIODS], probe_highsamples[PROBE_HPERIODS];
static unsigned long long probe_clock[PROBE_SAMPLES];
static u32 probe_errors, probe_raw_before, probe_bios_before;
static u32 probe_rx_count, probe_rx_missing, probe_rx_value, probe_rx_bytes, probe_rx_next;
static unsigned long long probe_previous;
static GFX_RasterPalTable probe_table;
static struct SplashRasterProbeCost probe_cost[PROBE_COST_KINDS];
static struct SplashRasterProbeEntry probe_entry[PROBE_ENTRIES];
static struct SplashRasterBudget probe_budget;
static u32 probe_wave, probe_period_ok, probe_phase_status, probe_phase_ambiguous, probe_entry_count;
static unsigned long long probe_f_lo, probe_f_hi, probe_h_lo, probe_h_hi;
static unsigned long long probe_eight_us, probe_high_us, probe_c_min;
static volatile u32 probe_poll_sink;
static unsigned long long probe_entry_upper, probe_retry_upper;
static unsigned long long probe_survey_if0_max, probe_survey_if0_total;
static unsigned long long probe_open_until;
static u32 probe_open_gap_us;

/* Timestamp sandwiches include the final clock call, not the first one.
 * Short IF=0 batches separate Q/C/W; IF=1 measurements retain IRQ pollution. */
static void probe_costs(const struct SplashRaster *s);
static void probe_classify(const struct SplashRaster *s);
static void probe_entries(const struct SplashRaster *s, long long initial_margin);
static void probe_receive(void);

void platform_splash_probe_before(void)
{
    probe_raw_before = inp(PEGC_HSYNC_PORT);
    probe_bios_before = *(volatile u8 *)P2V_IO(PEGC_BIOS_PRXCRT);
}
static unsigned long long probe_now(void)
{
    u32 lo, hi;
    if (sys_time_now(&lo, &hi)) { probe_errors++; return probe_previous; }
    unsigned long long t = ((unsigned long long)hi << BITS_PER_U32) | lo;
    if (t < probe_previous) probe_errors++;
    probe_previous = t;
    return t;
}
static void probe_poll_batch(u32 count)
{
    u32 old = inp(GDC_GFX_STAT), edges = 0;
    for (u32 i = 1; i < count; i++) {
        u32 status = inp(GDC_GFX_STAT);
        if (!(old & GDC_STAT_HBLANK) && (status & GDC_STAT_HBLANK)) edges++;
        old = status;
    }
    probe_poll_sink = edges;
}
static unsigned long long probe_cost_batch(u32 kind, u32 count)
{
    unsigned long long a = probe_now();
    if (kind == PROBE_Q) probe_poll_batch(count);
    else if (kind == PROBE_C) {
        for (u32 i = 1; i < count; i++) (void)probe_now();
    } else for (u32 i = 0; i < count; i++) palette_set(1, 15, 0, 0);
    return probe_now() - a;
}
/* Keep >=1H open between measurement bursts, including calibration. */
static int probe_measure_open(void)
{
    for (u32 n = 0; n < PROBE_POLLS; n++) {
        if (probe_now() >= probe_open_until) return 0;
        if (probe_errors) break;
    }
    probe_errors++;
    return -1;
}
static void probe_measure_restore(unsigned int flags, const struct SplashRaster *s)
{
    irq_restore(flags);
    probe_open_until = probe_now() + (s->line_num_us + s->line_den - 1) / s->line_den;
}
static unsigned long long probe_cost_bound(u32 kind, u32 den)
{
    const struct SplashRasterProbeCost *c = &probe_cost[kind];
    return (c->upper_num_us * den + c->upper_samples - 1) / c->upper_samples;
}
static void probe_costs(const struct SplashRaster *s)
{
    unsigned long long seed[PROBE_COST_KINDS] = {0};
    probe_c_min = ~0ULL;
    probe_open_until = 0;
    for (u32 i = 0; i < PROBE_COST_KINDS; i++)
        probe_cost[i] = (struct SplashRasterProbeCost){ .upper_samples = 1 };
    /* One-operation bootstrap sizes the later bursts; it is not the final
     * upper bound. Even the clocks at both boundaries count toward <=2H. */
    for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
        if (probe_measure_open()) return;
        unsigned int flags = irq_save();
        unsigned long long c = probe_cost_batch(PROBE_C, 1);
        probe_measure_restore(flags, s);
        if (c < probe_c_min) probe_c_min = c;
        if (c + SPLASH_RASTER_CLOCK_QUANTUM_US > seed[PROBE_C])
            seed[PROBE_C] = c + SPLASH_RASTER_CLOCK_QUANTUM_US;
    }
    for (u32 kind = PROBE_Q; kind <= PROBE_W; kind += PROBE_W) {
        if (probe_measure_open()) return;
        unsigned int flags = irq_save();
        unsigned long long elapsed = probe_cost_batch(kind, 1);
        probe_measure_restore(flags, s);
        seed[kind] = (elapsed > probe_c_min ? elapsed - probe_c_min : 0) +
                     2 * SPLASH_RASTER_CLOCK_QUANTUM_US;
    }
    const u32 order[PROBE_COST_KINDS] = { PROBE_C, PROBE_Q, PROBE_W };
    unsigned long long clock_lower = ~0ULL;
    u32 clock_samples = 1;
    for (u32 k = 0; k < PROBE_COST_KINDS; k++) {
        u32 kind = order[k];
        struct SplashRasterProbeCost *c = &probe_cost[kind];
        u32 count = PROBE_BURST;
        while (count > 1 && (count * seed[kind] + 2 * seed[PROBE_C]) * s->line_den >
                            2 * s->line_num_us) count /= 2;
        if ((count * seed[kind] + 2 * seed[PROBE_C]) * s->line_den > 2 * s->line_num_us) {
            probe_errors++; return;
        }
        c->batch = count;
        c->upper_samples = count * (kind == PROBE_C ? 1 : clock_samples);
        for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
            if (probe_measure_open()) return;
            unsigned int flags = irq_save();
            unsigned long long span = probe_cost_batch(kind, count);
            probe_measure_restore(flags, s);
            unsigned long long elapsed = kind == PROBE_C ? span :
                (span > probe_c_min ? span - probe_c_min : 0);
            c->total_us += elapsed;
            c->samples += count;
            unsigned long long upper;
            if (kind == PROBE_C) {
                unsigned long long lower = span > SPLASH_RASTER_CLOCK_QUANTUM_US ?
                    span - SPLASH_RASTER_CLOCK_QUANTUM_US : 0;
                if (lower < clock_lower) clock_lower = lower;
                upper = span + SPLASH_RASTER_CLOCK_QUANTUM_US;
            } else {
                /* Subtract the lower C batch mean as a ratio. Its quantum
                 * costs only 1/clock_samples, then 1/count after averaging. */
                unsigned long long raw = span * clock_samples;
                upper = (raw > clock_lower ? raw - clock_lower : 0) +
                        SPLASH_RASTER_CLOCK_QUANTUM_US * clock_samples;
            }
            if (upper > c->upper_num_us) c->upper_num_us = upper;
            elapsed = probe_cost_batch(kind, count); /* IF=1: IRQs kept separate */
            if (kind != PROBE_C) elapsed = elapsed > probe_c_min ? elapsed - probe_c_min : 0;
            c->open_total_us += elapsed;
            if (elapsed > c->open_max_us) c->open_max_us = elapsed;
        }
        c->upper_us = (c->upper_num_us + c->upper_samples - 1) / c->upper_samples;
        if (kind == PROBE_C) clock_samples = count;
    }
}
static unsigned long long probe_delta(unsigned long long a, unsigned long long b)
{
    return a > b ? a - b : b - a;
}
/* Capture only complete rise-to-rise sample runs. Clocks and IRQs between
 * bursts are never included in either the high count or its denominator. */
static u32 probe_hblank(const struct SplashRaster *s)
{
    unsigned long long q = probe_cost_bound(PROBE_Q, s->line_den);
    unsigned long long clocks = 2 * probe_cost_bound(PROBE_C, s->line_den);
    if (!q || clocks >= 2 * s->line_num_us) { probe_errors++; return 0; }
    u32 count = (2 * s->line_num_us - clocks) / q;
    if (count < 2) { probe_errors++; return 0; }
    unsigned long long deadline = probe_now() + SPLASH_RASTER_SYNC_TIMEOUT_US;
    u32 completed = 0;
    for (u32 burst = 0; burst < PROBE_POLLS && completed < PROBE_HPERIODS; burst++) {
        if (probe_measure_open() || probe_now() > deadline) break;
        unsigned int flags = irq_save();
        unsigned long long a = probe_now();
        u32 old = inp(GDC_GFX_STAT), rise = 0, highs = 0, armed = 0;
        for (u32 i = 1; i < count; i++) {
            u32 status = inp(GDC_GFX_STAT);
            if (!(old & GDC_STAT_HBLANK) && (status & GDC_STAT_HBLANK)) {
                if (armed && completed < PROBE_HPERIODS) {
                    probe_hsamples[completed] = i - rise;
                    probe_highsamples[completed++] = highs;
                }
                rise = i; highs = 0; armed = 1;
            }
            if (armed && (status & GDC_STAT_HBLANK)) highs++;
            old = status;
        }
        unsigned long long elapsed = probe_now() - a;
        probe_measure_restore(flags, s);
        if ((elapsed + probe_cost[PROBE_C].upper_us) * s->line_den > 2 * s->line_num_us) {
            probe_errors++; return 0;
        }
    }
    if (completed != PROBE_HPERIODS) probe_errors++;
    unsigned long long periods = SPLASH_RASTER_MEASURE_PERIODS * s->total_lines;
    for (u32 i = 0; i < completed; i++) {
        probe_hperiod[i] = (probe_hsamples[i] * probe_cost[PROBE_Q].total_us +
                           probe_cost[PROBE_Q].samples / 2) / probe_cost[PROBE_Q].samples;
        probe_hwidth[i] = (probe_highsamples[i] * probe_eight_us +
                          periods * probe_hsamples[i] / 2) / (periods * probe_hsamples[i]);
    }
    return completed;
}
static void probe_classify(const struct SplashRaster *s)
{
    probe_wave = SPLASH_RASTER_WAVE_UNKNOWN;
    probe_period_ok = 0;
    probe_eight_us = probe_edges[SPLASH_RASTER_MEASURE_PERIODS].rise - probe_edges[0].rise;
    probe_high_us = 0;
    for (u32 i = 0; i < SPLASH_RASTER_MEASURE_PERIODS; i++)
        probe_high_us += probe_edges[i].fall - probe_edges[i].rise;
    unsigned long long measured = probe_eight_us * s->line_den;
    unsigned long long expected = (unsigned long long)SPLASH_RASTER_MEASURE_PERIODS *
                                   s->total_lines * s->line_num_us;
    if (probe_delta(measured, expected) * SPLASH_RASTER_PERIOD_PERCENT >
        expected * SPLASH_RASTER_PERIOD_TOLERANCE) return;
    probe_period_ok = 1;
    measured = probe_high_us * s->line_den;
    unsigned long long tolerance = (unsigned long long)SPLASH_RASTER_MEASURE_PERIODS * s->line_num_us;
    if (probe_delta(measured, (s->total_lines - SPLASH_RASTER_VISIBLE_LINES) * tolerance) <= tolerance)
        probe_wave = s->mode == SPLASH_RASTER_31K ? SPLASH_RASTER_WAVE_31K_BLANK : SPLASH_RASTER_WAVE_24K_BLANK;
    else if (probe_delta(measured, s->vs_lines * tolerance) <= tolerance)
        probe_wave = s->mode == SPLASH_RASTER_31K ? SPLASH_RASTER_WAVE_31K_VS : SPLASH_RASTER_WAVE_24K_VS;
}
/* One IN supplies both V and H. Bracket the sample rather than assigning a
 * post-clock time to the edge. Starting high never counts as a fresh edge.
 * IF stays enabled; an IRQ widens the interval and cannot improve a budget. */
static int probe_phase(void)
{
    unsigned long long before = probe_now(), after, last = before;
    unsigned long long deadline = before + SPLASH_RASTER_SYNC_TIMEOUT_US;
    u32 old = inp(GDC_GFX_STAT), saw_high = 0, fell = 0, low = 0;
    probe_f_lo = probe_f_hi = probe_h_lo = probe_h_hi = 0;
    probe_phase_ambiguous = 0;
    for (u32 n = 0; n < PROBE_POLLS; n++) {
        before = probe_now();
        u32 status = inp(GDC_GFX_STAT);
        after = probe_now();
        if (probe_errors || after > deadline) break;
        if (status & GDC_STAT_VSYNC) saw_high = 1;
        if (saw_high && (old & GDC_STAT_VSYNC) && !(status & GDC_STAT_VSYNC)) {
            probe_f_lo = last; probe_f_hi = after; probe_phase_status = status;
            fell = 1;
            /* A simultaneous H transition is ambiguous; retain that fact. */
            if ((old ^ status) & GDC_STAT_HBLANK) probe_phase_ambiguous = 1;
        } else if (fell && low && !(old & GDC_STAT_HBLANK) && (status & GDC_STAT_HBLANK)) {
            probe_h_lo = last; probe_h_hi = after;
            return 0;
        }
        if (fell && !(status & GDC_STAT_HBLANK)) low = 1;
        old = status; last = before;
    }
    probe_errors++;
    return -1;
}
static long long probe_j_max(struct SplashRasterEntryBudget *b)
{
    if (b->usable == 3) return b->s[1].max - b->planned - b->retry;
    if (b->usable & 1) return b->s[0].max - b->planned;
    if (b->usable & 2) return b->s[1].max - b->planned;
    return -1;
}
/* Diagnostic entry sequence. The two clock checks are outside/inside the
 * saved-IF region respectively. A failed precheck creates no IF=0 region. */
static unsigned long long probe_enter(unsigned long long deadline)
{
    unsigned long long pre = probe_now();
    if (pre > deadline || pre < probe_open_until) return pre;
    unsigned int flags = irq_save();
    unsigned long long post = probe_now();
    if (post <= deadline) (void)inp(GDC_GFX_STAT);
    irq_restore(flags);
    unsigned long long end = probe_now(), bound = end - pre;
    probe_open_until = end + probe_open_gap_us;
    /* Includes both boundary clocks; a conservative observed IF=0 bound. */
    probe_survey_if0_total += bound;
    if (bound > probe_survey_if0_max) probe_survey_if0_max = bound;
    return post;
}
static void probe_entry_costs(const struct SplashRaster *s)
{
    /* Bound the survey entry sequence from real clock/IN measurements. The
     * future player's branch/retry bounds are not supplied by this survey. */
    probe_entry_upper = probe_retry_upper = 0;
    probe_survey_if0_max = probe_survey_if0_total = 0;
    probe_open_until = 0;
    probe_open_gap_us = (s->line_num_us + s->line_den - 1) / s->line_den;
    if ((3 * probe_cost[PROBE_C].upper_us + probe_cost[PROBE_Q].upper_us) * s->line_den <= 2 * s->line_num_us) {
        for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
            u32 poll;
            for (poll = 0; poll < PROBE_POLLS; poll++) {
                if (probe_now() >= probe_open_until) break;
                if (probe_errors) return;
            }
            if (poll == PROBE_POLLS) { probe_errors++; return; }
            unsigned long long a = probe_now();
            (void)probe_enter(~0ULL);
            unsigned long long span = probe_now() - a + SPLASH_RASTER_CLOCK_QUANTUM_US;
            if (span > probe_entry_upper) probe_entry_upper = span;
            a = probe_now();
            (void)probe_enter(0); /* overrun branch with IF=1 throughout */
            span = probe_now() - a + SPLASH_RASTER_CLOCK_QUANTUM_US;
            if (span > probe_retry_upper) probe_retry_upper = span;
        }
    }
}
/* Keep fractional cost bounds and the clock-free duty estimate in units
 * of the shared budget denominator. */
static void probe_bounds(const struct SplashRaster *s)
{
    u32 den = s->line_den;
    unsigned long long uncertainty = probe_cost[PROBE_Q].upper_us +
        2 * probe_cost[PROBE_C].upper_us + SPLASH_RASTER_CLOCK_QUANTUM_US;
    probe_budget = (struct SplashRasterBudget){ .den = den };
    probe_budget.q.max = probe_cost_bound(PROBE_Q, den);
    probe_budget.c.min = probe_c_min * den;
    probe_budget.c.max = probe_cost_bound(PROBE_C, den);
    probe_budget.w.max = probe_cost_bound(PROBE_W, den);
    unsigned long long divisor = SPLASH_RASTER_MEASURE_PERIODS * s->total_lines;
    probe_budget.h.min = (probe_eight_us > uncertainty ? probe_eight_us - uncertainty : 0) * den / divisor;
    probe_budget.h.max = ((probe_eight_us + uncertainty) * den + divisor - 1) / divisor;
    /* B below is an observed bit6 bound, not the physical reference B. */
    unsigned long long samples = 0, highs = 0;
    for (u32 i = 0; i < PROBE_HPERIODS; i++) {
        samples += probe_hsamples[i];
        highs += probe_highsamples[i];
    }
    /* Duty ratio times H, with one sample of edge uncertainty. C is absent:
     * neither endpoint clock is in the uninterrupted IN sample runs. */
    long long lo = samples ? highs * probe_budget.h.min / samples : 0;
    probe_budget.b.min = lo > probe_budget.q.max ? lo - probe_budget.q.max : 0;
    probe_budget.b.max = samples ? (highs * probe_budget.h.max + samples - 1) / samples +
                                 probe_budget.q.max : 0;
}
/* Survey the 99 four-line entry deadlines without issuing palette changes.
 * This is measurement, not the LC-B player: observed execution costs and
 * phase still require hardware qualification. J bounds include all gaps in
 * coarse polling, including successive IRQs and return latency. */
static void probe_entries(const struct SplashRaster *s, long long initial_margin)
{
    u32 den = s->line_den;
    probe_bounds(s);
    long long overhead = probe_entry_upper * den;
    probe_budget.error = SPLASH_RASTER_CLOCK_QUANTUM_US * den;
    probe_budget.retry = probe_retry_upper * den;
    long long h = s->line_num_us;
    long long capture = 2 * h - (long long)(probe_h_hi - probe_f_lo) * den - overhead;
    long long previous_finish = (long long)probe_h_hi * den + probe_budget.c.max;
    for (u32 i = 0; i < PROBE_ENTRIES; i++) {
        struct SplashRasterProbeEntry *e = &probe_entry[i];
        *e = (struct SplashRasterProbeEntry){ .line = (i + 1) * PROBE_BAND_LINES,
            .initial_margin = initial_margin, .capture_margin = capture, .rx_words = probe_rx_count };
        struct SplashRasterEntryBudget *b = &e->budget;
        u32 k = s->active0_lines + e->line;
        for (u32 n = 0; n < 2; n++) {
            u32 edge = k - 1 + n;
            /* I(edge) uses the first observed new H as k=1. */
            b->s[n].min = (long long)probe_h_hi * den + (edge - 2) * probe_budget.h.max +
                          probe_budget.b.max + overhead + probe_budget.error;
            b->s[n].max = (long long)probe_h_lo * den + (edge - 1) * probe_budget.h.min -
                          overhead - probe_budget.error;
            /* Retain only entrances whose worst completion fits 2H. */
            long long finish = (long long)probe_h_hi * den + (k - 1) * probe_budget.h.max +
                               probe_budget.w.max + probe_budget.c.max;
            if (b->s[n].min < finish - 2 * h) b->s[n].min = finish - 2 * h;
            if (b->s[n].min < previous_finish + h) b->s[n].min = previous_finish + h;
            if (probe_entry_upper && overhead <= 2 * h && b->s[n].min <= b->s[n].max)
                b->usable |= 1u << n;
        }
        b->planned = b->usable == 2 ? b->s[1].min : b->s[0].min;
        b->retry = probe_budget.retry;
        b->j_max = probe_j_max(b);
        e->frame_j_max = b->j_max < capture ? b->j_max : capture;
        if (initial_margin < e->frame_j_max) e->frame_j_max = initial_margin;
        e->planned_us = b->planned > 0 ? (b->planned + den - 1) / den : 0;
        unsigned long long last = probe_now(), now = last;
        e->wait_begin_us = last;
        for (u32 n = 0; now < e->planned_us && n < PROBE_POLLS; n++) {
            now = probe_now();
            unsigned long long span = now - last;
            if (span > probe_cost[PROBE_C].upper_us)
                e->irq_us.min += span - probe_cost[PROBE_C].upper_us;
            if (span > probe_c_min) e->irq_us.max += span - probe_c_min;
            last = now;
            if (probe_errors) return;
        }
        if (now < e->planned_us) { probe_errors++; return; }
        e->entered_us = now;
        /* Exercise the entry precheck/save/recheck/first-IN sequence only
         * when its measured cost fits the <=2H contract. No palette OUT. */
        if (b->usable && (long long)now * den <= b->s[1].max) {
            e->entered_us = probe_enter(b->s[1].max / den);
            unsigned long long span = e->entered_us - now;
            if (span > 2 * probe_cost[PROBE_C].upper_us)
                e->irq_us.min += span - 2 * probe_cost[PROBE_C].upper_us;
            if (span > 2 * probe_c_min) e->irq_us.max += span - 2 * probe_c_min;
        }
        e->end_us = probe_now();
        /* LC-A never sets qualified: F/H ordering, branch/D and IF windows
         * need LC-B plus the physical ruler. Preserve candidates for review. */
        probe_entry_count++;
        previous_finish = (long long)probe_h_hi * den + (k - 1) * probe_budget.h.max +
                          probe_budget.w.max + probe_budget.c.max;
        probe_receive();
    }
}
static int probe_bit(u32 port, u32 bit, int high, unsigned long long *stamp)
{
    unsigned long long deadline = probe_now() + SPLASH_RASTER_SYNC_TIMEOUT_US;
    for (u32 n = 0; n < PROBE_POLLS; n++) {
        u32 status = inp(port);
        *stamp = probe_now();
        if (probe_errors || *stamp > deadline) break;
        if (!!(status & bit) == high) return 0;
    }
    probe_errors++;
    return -1;
}
static void probe_field(const char *key, unsigned long long n)
{
    /* All timestamps are absolute; print high:low decimal words losslessly. */
    serial_puts_polled(key);
    if (n >> BITS_PER_U32) { serial_diag_u32(n >> BITS_PER_U32); serial_puts_polled(":"); }
    serial_diag_u32(n);
}
static void probe_signed(const char *key, long long n)
{
    serial_puts_polled(key);
    if (n < 0) serial_puts_polled("-");
    probe_field("", n < 0 ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n);
}
static void probe_stats(const char *key, unsigned long long *v, u32 n)
{
    unsigned long long total = 0;
    for (u32 i = 0; i < n; i++) total += v[i];
    for (u32 i = 1; i < n; i++) {
        unsigned long long x = v[i];
        u32 j = i;
        while (j && v[j-1] > x) { v[j] = v[j-1]; j--; }
        v[j] = x;
    }
    serial_puts_polled("SPLR sample="); serial_puts_polled(key);
    probe_field(" n=", n); probe_field(" min=", v[0]);
    probe_field(" median=", v[n/2]); probe_field(" p99=", v[(n*99+99)/100-1]);
    probe_field(" max=", v[n-1]); probe_field(" total=", total); serial_puts_polled("\n");
}
static void probe_pattern(void)
{
    platform_splash_clear(0);
    platform_splash_fill_rect(PROBE_CENTER_X, 0, PROBE_CENTER_W, GFX_HEIGHT, 1);
    for (int y = 0; y < GFX_HEIGHT; y += PROBE_MARK)
        platform_splash_fill_rect(0, y, PROBE_CENTER_X, y % PROBE_BIG_MARK ? 1 : PROBE_BORDER, 15);
    for (int col = -PROBE_SHIFT; col <= PROBE_SHIFT; col++)
        for (int y = 0; y < GFX_HEIGHT; y++)
            if (((y + PROBE_STEP + col) / PROBE_STEP) & 1)
                platform_splash_fill_rect(PROBE_RULER_X + (col + PROBE_SHIFT)*PROBE_RULER_W,
                                          y, PROBE_RULER_W, 1, 15);
    for (u32 i = 0; i < OSRI_COLORS; i++)
        platform_splash_fill_rect(PROBE_TILE_X, i * (GFX_HEIGHT / OSRI_COLORS),
                                 GFX_WIDTH - PROBE_TILE_X, GFX_HEIGHT / OSRI_COLORS, i);
    platform_splash_fill_rect(0, 0, GFX_WIDTH, PROBE_BORDER, 15);
    platform_splash_fill_rect(0, GFX_HEIGHT - PROBE_BORDER, GFX_WIDTH, PROBE_BORDER, 15);
    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present_nosync();
}
/* Load sender: consecutive little-endian u32 words, starting at zero. Drain
 * between frames only; the ISR and its existing ring remain unchanged. */
static void probe_receive(void)
{
    for (u32 n = 0; n < PROBE_RX_BYTES; n++) {
        int c = serial_trygetchar();
        if (c < 0) break;
        probe_rx_value |= (u32)(u8)c << (probe_rx_bytes * BITS_PER_BYTE);
        if (++probe_rx_bytes == sizeof(u32)) {
            if (probe_rx_value != probe_rx_next) probe_rx_missing++;
            probe_rx_next = probe_rx_value + 1;
            probe_rx_count++;
            probe_rx_bytes = probe_rx_value = 0;
        }
    }
}
void platform_splash_probe(void)
{
    struct SplashRaster *s = pc98_splash_raster_state();
    const struct bootinfo *bi = bootinfo_get();
    u32 raw_after = inp(PEGC_HSYNC_PORT);
    u32 bios_after = *(volatile u8 *)P2V_IO(PEGC_BIOS_PRXCRT);
    u32 mode, baud, fifo, oe0, fe0, pe0, ov0, oe, fe, pe, ov;
    u32 clamp0 = ktime_clamp_count, tick0 = tick_count, ticks, clamps;
    unsigned long long stamp, io[3][3] = {{0}}, empty[3] = {0}, initial_begin = 0, initial_end = 0, initial_fall = 0;
    long long initial_margin = -1;
    u32 initial_status = 0, completed = 0, hcompleted = 0, missed_h = 0;
    const u32 counts[3] = {1, 4, 8};
    if (serial_gate_active() || !_irq_enabled()) return;
    serial_get_status(&mode, &baud, &fifo);
    serial_diag_get(&oe0, &fe0, &pe0, &ov0);
    probe_errors = probe_rx_count = probe_rx_missing = probe_rx_bytes = probe_rx_value = probe_rx_next = 0;
    probe_previous = 0;
    probe_entry_count = probe_wave = probe_period_ok = probe_phase_status = 0;
    probe_eight_us = probe_high_us = 0;
    probe_budget = (struct SplashRasterBudget){0};
    probe_entry_upper = probe_retry_upper = probe_survey_if0_max = probe_survey_if0_total = 0;
    probe_f_lo = probe_f_hi = probe_h_lo = probe_h_hi = 0;
    for (u32 i = 0; i < PROBE_FRAMES; i++) probe_frames[i] = (struct probe_frame){0};
    for (u32 i = 0; i <= PROBE_FRAMES; i++) probe_edges[i] = (struct probe_edge){0};
    probe_pattern();
    probe_costs(s);
    for (u32 i = 0; i < PROBE_SAMPLES; i++) {
        unsigned long long a = probe_now();
        probe_clock[i] = probe_now() - a;
    }
    /* Observe both GDCs together: waiting for one must not hide the other. */
    u32 text_edges = 0;
    u32 old_g = inp(GDC_GFX_STAT), old_t = inp(GDC_TEXT_STAT);
    unsigned long long edge_deadline = probe_now() +
        (PROBE_FRAMES + 1) * SPLASH_RASTER_SYNC_TIMEOUT_US;
    for (u32 poll = 0; poll < PROBE_POLLS; poll++) {
        u32 g = inp(GDC_GFX_STAT), t = inp(GDC_TEXT_STAT);
        stamp = probe_now();
        if (probe_errors || stamp > edge_deadline) break;
        if (!(old_g & GDC_STAT_VSYNC) && (g & GDC_STAT_VSYNC) && completed <= PROBE_FRAMES)
            probe_edges[completed++].rise = stamp;
        if ((old_g & GDC_STAT_VSYNC) && !(g & GDC_STAT_VSYNC) && completed)
            probe_edges[completed-1].fall = stamp;
        if (!(old_t & GDC_STAT_VSYNC) && (t & GDC_STAT_VSYNC) && text_edges <= PROBE_FRAMES)
            probe_edges[text_edges++].text = stamp;
        if ((old_t & GDC_STAT_VSYNC) && !(t & GDC_STAT_VSYNC) && text_edges)
            probe_edges[text_edges-1].text_fall = stamp;
        old_g = g; old_t = t;
        if (completed == PROBE_FRAMES + 1 && text_edges == PROBE_FRAMES + 1 &&
            probe_edges[PROBE_FRAMES].fall && probe_edges[PROBE_FRAMES].text_fall) break;
    }
    if (completed != PROBE_FRAMES + 1 || text_edges != PROBE_FRAMES + 1 ||
        !probe_edges[PROBE_FRAMES].fall || !probe_edges[PROBE_FRAMES].text_fall) probe_errors++;
    if (probe_errors) goto dump;
    probe_classify(s);
    hcompleted = probe_hblank(s);
    if (probe_errors) goto dump;
    /* 8/16 single OUTs; 1/4/8 four-OUT colors; matching empty-loop cost.
     * Leave IF enabled, including all sixteen initial colors. */
    for (u32 k = 0; k < 3; k++) {
        stamp = probe_now();
        for (volatile u32 j = 0; j < counts[k]; j++) { }
        empty[k] = probe_now() - stamp;
        stamp = probe_now();
        for (u32 j = 0; j < counts[k]; j++) palette_set(1, 15, 0, 0);
        io[0][k] = probe_now() - stamp;
        stamp = probe_now();
        for (u32 j = 0; j < (k ? 16u : 8u); j++) outp(PAL_IDX_PORT, 1);
        io[1][k] = probe_now() - stamp;
        io[2][k] = counts[k];
    }
    if (probe_bit(GDC_GFX_STAT, GDC_STAT_VSYNC, 0, &stamp) ||
        probe_bit(GDC_GFX_STAT, GDC_STAT_VSYNC, 1, &stamp)) goto dump;
    /* VS-type writes in VBP, not in the short VS pulse. Blank-type writes
     * at V rise. Predict a deadline even if an IRQ misses the actual fall. */
    initial_fall = stamp + probe_high_us / SPLASH_RASTER_MEASURE_PERIODS;
    if (probe_wave == SPLASH_RASTER_WAVE_24K_VS || probe_wave == SPLASH_RASTER_WAVE_31K_VS)
        if (probe_bit(GDC_GFX_STAT, GDC_STAT_VSYNC, 0, &stamp)) goto dump;
    initial_begin = probe_now();
    for (u32 i = 0; i < OSRI_COLORS; i++) palette_set(i, i, i, i);
    initial_end = probe_now(); initial_status = inp(GDC_GFX_STAT);
    initial_margin = ((long long)initial_fall - (long long)initial_end) * s->line_den +
        (long long)s->active0_lines * s->line_num_us -
        (long long)(probe_cost[PROBE_Q].upper_us + 2 * probe_cost[PROBE_C].upper_us) * s->line_den;
    /* Blank-type initial colors must finish before the earliest origin
     * capture entrance, not merely before visible line zero. */
    if (probe_wave == SPLASH_RASTER_WAVE_24K_BLANK || probe_wave == SPLASH_RASTER_WAVE_31K_BLANK)
        initial_margin -= 2 * s->line_num_us;
    probe_entry_costs(s);
    if (!probe_phase() && hcompleted == PROBE_HPERIODS)
        probe_entries(s, initial_margin);
    /* Nine one-line phase shifts, then steady 4-line bands. No OSRI events. */
    for (u32 frame = 0; frame < PROBE_FRAMES; frame++) {
        probe_table.count = 1;
        probe_table.entries[0] = (GFX_RasterPalEntry){ .line = 0, .pal_idx = 1, .r = 15 };
        int sweeping = frame < 2 * PROBE_SHIFT + 1;
        int shift = sweeping ? (int)frame - PROBE_SHIFT : 0;
        /* Sweep the first edge across 4..12, keeping the line-zero gap valid. */
        int first_line = sweeping ? 2 * PROBE_SHIFT : PROBE_BAND_LINES;
        for (int y = first_line; y < GFX_HEIGHT; y += PROBE_BAND_LINES) {
            int line = y + shift;
            if (line >= GFX_HEIGHT) continue;
            int red = ((y - first_line) / PROBE_BAND_LINES) & 1;
            probe_table.entries[probe_table.count++] = (GFX_RasterPalEntry){
                .line = line, .pal_idx = 1, .r = red ? 15 : 0,
                .g = red ? 0 : 15 };
        }
        u32 tick = tick_count, late = s->late, drop = s->dropped, unapplied = s->unapplied;
        stamp = probe_now();
        int rc = pc98_splash_raster_table(s, &probe_table);
        const struct SplashRasterDiag *d = pc98_splash_raster_diag();
        probe_frames[frame] = (struct probe_frame){ .duration = probe_now() - stamp,
            .closed_max = d->if0_max_us, .closed_total = d->if0_total_us,
            .late = s->late - late, .dropped = s->dropped - drop,
            .unapplied = s->unapplied - unapplied, .tick = tick_count - tick };
        if (rc || d->overflow || d->clock_errors) { probe_errors++; break; }
        probe_receive();
    }
dump:
    ticks = tick_count - tick0; clamps = ktime_clamp_count - clamp0;
    serial_diag_get(&oe, &fe, &pe, &ov);
    serial_puts_polled("SPLR commit="); serial_puts_polled(os32_build_commit);
    probe_field(" crc=", bi->img_crc); probe_field(" source=", bi->source);
    probe_field(" raw_before=", probe_raw_before); probe_field(" raw_after=", raw_after);
    probe_field(" d0=", raw_after & PEGC_HSYNC_READ_HF);
    probe_field(" bios_before=", probe_bios_before & PEGC_BIOS_PRXCRT_31KHZ);
    probe_field(" bios_after=", bios_after & PEGC_BIOS_PRXCRT_31KHZ);
    probe_field(" height=", gfx_current_height); probe_field(" flip=", gfx_flip_enabled);
    probe_field(" pit_reload=", pit_get_setup()->reload);
    probe_field(" baud=", baud); probe_field(" serial_mode=", mode); probe_field(" fifo=", fifo);
    probe_field(" active0=", s->active0_lines); probe_field(" dynamic_ok=", s->dynamic_ok);
    serial_puts_polled("\n");
    probe_field("SPLR wave=", probe_wave); probe_field(" periods=", SPLASH_RASTER_MEASURE_PERIODS);
    probe_field(" period_ok=", probe_period_ok); probe_field(" eight_us=", probe_eight_us);
    probe_field(" high_sum_us=", probe_high_us); probe_field(" f_lo=", probe_f_lo);
    probe_field(" f_hi=", probe_f_hi); probe_field(" h_lo=", probe_h_lo);
    probe_field(" h_hi=", probe_h_hi); probe_field(" vh_at_f=", probe_phase_status);
    probe_field(" phase_ambiguous=", probe_phase_ambiguous);
    serial_puts_polled("\n");
    for (u32 i = 0; i < PROBE_COST_KINDS; i++) {
        probe_field("SPLR cost=", i); probe_field(" num_us=", probe_cost[i].total_us);
        probe_field(" den=", probe_cost[i].samples); probe_field(" upper_us=", probe_cost[i].upper_us);
        probe_field(" upper_num_us=", probe_cost[i].upper_num_us);
        probe_field(" upper_den=", probe_cost[i].upper_samples);
        probe_field(" batch=", probe_cost[i].batch);
        probe_field(" if1_total_us=", probe_cost[i].open_total_us);
        probe_field(" if1_max_batch_us=", probe_cost[i].open_max_us);
        serial_puts_polled("\n");
    }
    unsigned long long h_samples = 0, h_highs = 0;
    for (u32 i = 0; i < hcompleted; i++) {
        h_samples += probe_hsamples[i];
        h_highs += probe_highsamples[i];
    }
    probe_field("SPLR h_samples=", h_samples); probe_field(" high_samples=", h_highs);
    serial_puts_polled("\n");
    probe_field("SPLR budget_den=", probe_budget.den);
    probe_field(" entry_upper_us=", probe_entry_upper); probe_field(" retry_upper_us=", probe_retry_upper);
    probe_field(" survey_if0_max_us=", probe_survey_if0_max);
    probe_field(" survey_if0_total_us=", probe_survey_if0_total);
    probe_signed(" h_min=", probe_budget.h.min); probe_signed(" h_max=", probe_budget.h.max);
    probe_signed(" b_min=", probe_budget.b.min); probe_signed(" b_max=", probe_budget.b.max);
    probe_signed(" clock_error=", probe_budget.error);
    probe_field(" b_reference_num=", s->mode == SPLASH_RASTER_31K ?
        SPLASH_RASTER_31K_BLANK_NUM_US : SPLASH_RASTER_24K_BLANK_NUM_US);
    probe_field(" b_reference_den=", s->mode == SPLASH_RASTER_31K ?
        SPLASH_RASTER_31K_DEN : SPLASH_RASTER_24K_BLANK_DEN);
    serial_puts_polled("\n");
    long long frame_j_max = probe_entry_count ? probe_entry[0].frame_j_max : -1;
    long long observed_j_max = 0;
    for (u32 i = 0; i < probe_entry_count; i++) {
        const struct SplashRasterProbeEntry *e = &probe_entry[i];
        const struct SplashRasterEntryBudget *b = &e->budget;
        if (e->frame_j_max < frame_j_max) frame_j_max = e->frame_j_max;
        if (e->irq_us.max > observed_j_max) observed_j_max = e->irq_us.max;
        probe_field("SPLR entry_y=", e->line); probe_field(" den=", probe_budget.den);
        probe_field(" wait_begin_us=", e->wait_begin_us);
        probe_field(" planned_us=", e->planned_us); probe_field(" entered_us=", e->entered_us);
        probe_field(" end_us=", e->end_us); probe_field(" rx_words=", e->rx_words);
        probe_signed(" j_lo_us=", e->irq_us.min); probe_signed(" j_hi_us=", e->irq_us.max);
        probe_signed(" l0=", b->s[0].min); probe_signed(" u0=", b->s[0].max);
        probe_signed(" l1=", b->s[1].min); probe_signed(" u1=", b->s[1].max);
        probe_signed(" r=", b->planned); probe_signed(" d=", b->retry);
        probe_field(" usable=", b->usable); probe_signed(" j_max=", b->j_max);
        probe_signed(" capture_margin=", e->capture_margin);
        probe_signed(" initial_margin=", e->initial_margin);
        probe_field(" qualified=", e->qualified); serial_puts_polled("\n");
    }
    probe_signed("SPLR frame_j_max=", frame_j_max); probe_field(" den=", probe_budget.den);
    probe_signed(" observed_j_max_us=", observed_j_max); probe_field(" samples=", probe_entry_count);
    serial_puts_polled("\n");
    for (u32 i = 0; i < completed; i++) {
        probe_field("SPLR edge=", i); probe_field(" rise=", probe_edges[i].rise);
        probe_field(" fall=", probe_edges[i].fall); probe_field(" text=", probe_edges[i].text);
        probe_field(" text_fall=", probe_edges[i].text_fall);
        serial_puts_polled("\n");
    }
    probe_stats("clock_read_us", probe_clock, PROBE_SAMPLES);
    if (completed > 1) {
        for (u32 i = 1; i < completed; i++) probe_clock[i-1] = probe_edges[i].rise - probe_edges[i-1].rise;
        probe_stats("vsync_us", probe_clock, completed - 1);
    }
    if (hcompleted) {
        probe_stats("hperiod_us", probe_hperiod, hcompleted);
        probe_stats("hwidth_observed_us", probe_hwidth, hcompleted);
    }
    for (u32 k = 0; k < 3; k++) {
        probe_field("SPLR io_colors=", io[2][k]); probe_field(" colors_us=", io[0][k]);
        probe_field(" out_count=", k ? 16 : 8); probe_field(" out_us=", io[1][k]); probe_field(" empty_us=", empty[k]); serial_puts_polled("\n");
    }
    probe_field("SPLR initial_begin=", initial_begin); probe_field(" initial_end=", initial_end);
    probe_field(" status=", initial_status); probe_field(" fall=", initial_fall);
    probe_signed(" active_margin_num=", initial_margin); probe_field(" den=", s->line_den);
    serial_puts_polled("\n");
    for (u32 i = 0; i < PROBE_FRAMES; i++) {
        const struct probe_frame *f = &probe_frames[i];
        probe_field("SPLR frame=", i); probe_field(" duration=", f->duration);
        probe_field(" if0_max_us=", f->closed_max); probe_field(" if0_total_us=", f->closed_total);
        probe_field(" late=", f->late); probe_field(" drop=", f->dropped);
        probe_field(" unapplied=", f->unapplied); probe_field(" tick=", f->tick); serial_puts_polled("\n");
    }
    const struct SplashRasterDiag *d = pc98_splash_raster_diag();
    for (u32 i = 0; i < d->count; i++) {
        const struct SplashRasterEventDiag *e = &d->events[i];
        probe_field("SPLR event=", i); probe_field(" y=", e->line);
        probe_field(" target_us=", e->target_us); probe_field(" begin_us=", e->begin_us);
        probe_field(" end_us=", e->end_us); probe_field(" applied=", e->applied); serial_puts_polled("\n");
    }
    probe_field("SPLR errors=", probe_errors); probe_field(" missing_h=", missed_h);
    probe_field(" clamps=", clamps); probe_field(" ticks=", ticks);
    probe_field(" oe=", oe - oe0); probe_field(" fe=", fe - fe0); probe_field(" pe=", pe - pe0);
    probe_field(" overflow=", ov - ov0); probe_field(" rx_words=", probe_rx_count);
    probe_field(" rx_discontinuities=", probe_rx_missing); probe_field(" rx_partial=", probe_rx_bytes);
    serial_puts_polled("\n");
}
#endif
