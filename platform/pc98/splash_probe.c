#include "pit.h"
/* Diagnostic-only foreground scan measurements. No output inside a scan.
 * I/O: FreeBSD releng/10.4 sys/pc98/cbus/gdc.c:655,685,831 (BSD-3-Clause).
 * Timing: NEC Technical Databook Hardware (1993), p.350-351; design section 2. */
#ifdef SPLASH_RASTER_DIAG
#include "platform_splash.h"
#include "gfx_osri.h"
#include "splash_raster.h"
#include "splash_raster_image.h"
#include "gfx.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "palette.h"
#include "pc98.h"
#include "pegc.h"
#include "v86_bios.h"
#include "io.h"
#include "sys.h"
#include "serial.h"
#include "serial_diag.h"
#include "bootinfo.h"
#include "build_id.h"
#include "idt.h"
#include "ktime.h"
#include "config.h"

#ifndef PROBE_FRAMES
#define PROBE_FRAMES 32u
#endif
#ifndef PROBE_HPERIODS
#define PROBE_HPERIODS 64u
#endif
#ifndef PROBE_POLLS
#define PROBE_POLLS 1000000u
#endif
#ifndef PROBE_SAMPLES
#define PROBE_SAMPLES 128u
#endif
#ifndef PROBE_HSAMPLES
#define PROBE_HSAMPLES 8192u
#endif
#define PROBE_PL_COLUMNS 129u /* N5:42, N6:87; PL design M1 addendum. */
#define PROBE_PL_MASKS 7u
#define PROBE_PL_Q 0u
#define PROBE_PL_D 1u
#define PROBE_PL_W 2u
#define PROBE_PL_EMPTY 3u
#define PROBE_PL_PREP 4u
#define PROBE_PL_KINDS 5u
#define PROBE_PL_TRANS 5u
#define PROBE_PL_JOINT 6u
#define PROBE_PL_SLOTS 7u
#define PROBE_PL_PARTS 4u /* poll batch, single seed, prep, successful wait linkage */
#define PROBE_PL_STAMP_BEGIN 1u
#define PROBE_PL_STAMP_END 2u
#define PROBE_H_PER_LINE 4u
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
#define PROBE_RAW_READS 4096u
#define PROBE_RAW_BLOCKS 8u
#define PROBE_RAW_VALUES 256u
#define PROBE_RAW_PER_LINE 16u
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
/* Stable diagnostic reason IDs; zero means no error. Context is captured in
 * RAM only: serial output must stay outside all measurements and IF=0. */
enum probe_error_reason {
    PROBE_ERR_NONE = 0,
    PROBE_ERR_PL_SUCCESS = 1,
    PROBE_ERR_CLOCK_READ = 2,
    PROBE_ERR_CLOCK_BACKWARD = 3,
    PROBE_ERR_OPEN_WAIT = 4,
    PROBE_ERR_COST_BUDGET = 5,
    PROBE_ERR_H_BUDGET = 6,
    PROBE_ERR_H_CALIBRATE = 7,
    PROBE_ERR_H_CAPACITY = 8,
    PROBE_ERR_H_COUNT = 9,
    PROBE_ERR_H_OVERRUN = 10,
    PROBE_ERR_H_INCOMPLETE = 11,
    PROBE_ERR_PHASE_WAIT = 12,
    PROBE_ERR_ENTRY_OPEN = 13,
    PROBE_ERR_PL_ALIGN = 14,
    PROBE_ERR_PL_SEED_BUDGET = 15,
    PROBE_ERR_PL_BATCH_BUDGET = 16,
    PROBE_ERR_ENTRY_WAIT = 17,
    PROBE_ERR_BIT_WAIT = 18,
    PROBE_ERR_V_INCOMPLETE = 19,
    PROBE_ERR_TABLE_DIAG = 20,
    PROBE_ERR_COUNT
};
enum probe_scan_reason { PROBE_SCAN_COMPLETE = 1, PROBE_SCAN_ERROR = 5, PROBE_SCAN_AFTER_ERROR = 6 };
#define PROBE_CONTEXT_NONE (~0u)
static u32 probe_fatal, probe_error_counts[PROBE_ERR_COUNT];
static struct probe_error_context {
    u32 col, phase, n, kind;
} probe_context;
static struct {
    enum probe_error_reason reason;
    struct probe_error_context context;
    unsigned long long value;
} probe_error_first;
static void probe_error_reset(void)
{
    probe_errors = probe_fatal = 0;
    for (u32 i = 0; i < PROBE_ERR_COUNT; i++) probe_error_counts[i] = 0;
    probe_error_first.reason = PROBE_ERR_NONE;
    probe_context = (struct probe_error_context){ PROBE_CONTEXT_NONE,
        PROBE_CONTEXT_NONE, PROBE_CONTEXT_NONE, PROBE_CONTEXT_NONE };
}
static void probe_error(enum probe_error_reason reason, unsigned long long value)
{
    if (!probe_errors) {
        probe_error_first.reason = reason;
        probe_error_first.context = probe_context;
        probe_error_first.value = value;
    }
    probe_errors++;
    probe_error_counts[reason]++;
    if (reason != PROBE_ERR_PL_SUCCESS && reason != PROBE_ERR_PL_ALIGN &&
        reason != PROBE_ERR_PL_SEED_BUDGET && reason != PROBE_ERR_PL_BATCH_BUDGET)
        probe_fatal = 1;
}
static u32 probe_rx_count, probe_rx_missing, probe_rx_value, probe_rx_bytes, probe_rx_next;
static unsigned long long probe_previous;
static GFX_RasterPalTable probe_table;
static struct SplashRasterProbeCost probe_cost[PROBE_COST_KINDS];
static struct SplashRasterProbeEntry probe_entry[PROBE_ENTRIES];
static struct SplashRasterBudget probe_budget;
static u32 probe_wave, probe_period_ok, probe_phase_status, probe_phase_ambiguous, probe_entry_count;
static unsigned long long probe_f_lo, probe_f_hi, probe_h_lo, probe_h_hi;
static unsigned long long probe_eight_us, probe_high_us, probe_c_min;
static volatile u8 probe_hstatus[PROBE_HSAMPLES];
static unsigned long long probe_q_lower; /* scaled by line_den */
static unsigned long long probe_entry_upper, probe_retry_upper;
static unsigned long long probe_survey_if0_max, probe_survey_if0_total;
static unsigned long long probe_open_until;
static u32 probe_open_gap_us;
static unsigned long long probe_now(void);

/* Fixed before scanning; the two rows differ only in component value.
 * No mask expansion or column selection takes place inside H. */
typedef void (*probe_pl_emit_fn)(const struct PlRow *);
/* Corrected per-operation batch upper bounds, excluding the single seed.
 * second is the greatest value strictly below max; ties count at max. */
struct probe_pl_peak {
    long long max, second;
    unsigned long long seed;
    u32 hits, batches, second_valid, count;
};
struct probe_pl_column {
    struct PlRow row[2];
    u32 colors, n;
    struct SplashRasterTimeRange poll;
    long long d, w, prep;
    struct probe_pl_peak peak[2][PROBE_PL_SLOTS];
    long long empty_min[2];
    unsigned long long elapsed_us;
    u32 completed, early, timeout, high_after, low_after, window_ok, scan_reason;
};
static struct probe_pl_column probe_pl_columns[PROBE_PL_COLUMNS];
static struct probe_pl_result {
    struct SplashRasterImageBudget budget;
    struct SplashRasterTimeRange poll;
    long long prep, transition, normal, miss, tmin, tmax, early_time;
    u32 columns, lower, upper, window_ok, qualified;
} probe_pl_result[2];
static u32 probe_pl_count;
struct probe_pl_detail {
    struct probe_pl_peak raw, upper, correction;
};
static struct probe_pl_detail probe_pl_detail[2][PROBE_PL_PARTS];
/* Representative: prefixes of the first N6 column (N3 uses G/R/B), chosen before timing.
 * These extra measurements do not supply or change an adoption budget. */
static struct probe_pl_column probe_pl_joint[PROBE_PL_SLOTS];
static u32 probe_pl_joint_reason[PROBE_PL_SLOTS][2];
static struct probe_pl_detail probe_pl_outside[PROBE_PL_SLOTS][2];
static struct probe_pl_peak probe_pl_joint_polls[PROBE_PL_SLOTS][2];
struct probe_pl_stamp { unsigned long long begin, end; u32 iterations, clocks; };
static unsigned long long probe_pl_batch_total, probe_pl_batch_outside;
static u32 probe_pl_batch_polls, probe_pl_edge_upper;

#define PROBE_NOINLINE __attribute__((noinline, noclone))
#define PROBE_PL_OUT(i) outp(r->port[i], r->value[i])
static PROBE_NOINLINE void probe_pl_emit1(const struct PlRow *r) { PROBE_PL_OUT(0); }
static PROBE_NOINLINE void probe_pl_emit2(const struct PlRow *r) { PROBE_PL_OUT(0); PROBE_PL_OUT(1); }
static PROBE_NOINLINE void probe_pl_emit3(const struct PlRow *r)
{ PROBE_PL_OUT(0); PROBE_PL_OUT(1); PROBE_PL_OUT(2); }
static PROBE_NOINLINE void probe_pl_emit4(const struct PlRow *r)
{ PROBE_PL_OUT(0); PROBE_PL_OUT(1); PROBE_PL_OUT(2); PROBE_PL_OUT(3); }
static PROBE_NOINLINE void probe_pl_emit5(const struct PlRow *r)
{
    PROBE_PL_OUT(0); PROBE_PL_OUT(1); PROBE_PL_OUT(2);
    PROBE_PL_OUT(3); PROBE_PL_OUT(4);
}
static PROBE_NOINLINE void probe_pl_emit6(const struct PlRow *r)
{
    PROBE_PL_OUT(0); PROBE_PL_OUT(1); PROBE_PL_OUT(2);
    PROBE_PL_OUT(3); PROBE_PL_OUT(4); PROBE_PL_OUT(5);
}
#undef PROBE_PL_OUT
static PROBE_NOINLINE void probe_pl_empty(const struct PlRow *r)
{
    __asm__ volatile("" : : "r"(r) : "memory");
}
/* Return the successful IN's iteration, or zero at the finite upper limit.
 * The same instruction path is calibrated with an impossible mask/want. */
static PROBE_NOINLINE u32 probe_pl_wait(u32 mask, u32 want, u32 limit)
{
    for (u32 i = 1; i < limit; i++) {
        u32 status = inp(GDC_GFX_STAT);
        if ((status & mask) == want) return i;
    }
    return 0;
}
static PROBE_NOINLINE int probe_pl_commit(const struct PlRow *r,
    probe_pl_emit_fn emit, u32 polls, u32 lower, u32 upper)
{
    if (polls < lower || polls >= upper) return -1;
    emit(r);
    return 0;
}
static PROBE_NOINLINE const struct PlRow *probe_pl_prepare(
    const struct probe_pl_column *c, u32 phase)
{
    const struct PlRow *r = &c->row[phase & 1];
    outp(PAL_IDX_PORT, r->first);
    return r;
}
/* Observe an actual low before accepting high, as in R1 next_edge.
 * Clocks bracket the whole finite wait, outside its loop. Thus the body
 * interval bounds successful IN -> last OUT conservatively: it also contains
 * the preceding low/unsuccessful polls (reported separately as a count).
 * Entry/return/caller control is measured in disjoint outer intervals, never
 * hidden in this body or removed with an EMPTY differential. These M1 costs
 * overlap Q and D+W; they do not certify R1's final machine instructions. */
/* noipa is essential: all benchmark arguments are fixed, and noclone alone
 * lets GCC delete the success/upper/lower branches by propagating constants. */
static __attribute__((noipa)) int probe_pl_success(const struct PlRow *r,
    probe_pl_emit_fn emit, u32 mask, u32 want, u32 lower, u32 upper,
    int armed, int require_low, u32 *iterations, struct probe_pl_stamp *stamp)
{
    int result = -1;
    if (stamp->clocks & PROBE_PL_STAMP_BEGIN) stamp->begin = probe_now();
    for (u32 i = 1; ; i++) {
        u32 status = inp(GDC_GFX_STAT);
        if (i >= upper) { *iterations = i; break; }
        if (i == 1 && require_low && (status & mask) == want) {
            *iterations = i;
            break;
        }
        if ((status & mask) != want) armed = 1;
        else if (armed) {
            *iterations = i;
            if (i < lower) break;
            emit(r);
            result = 0;
            break;
        }
    }
    if (stamp->clocks & PROBE_PL_STAMP_END) stamp->end = probe_now();
    stamp->iterations = *iterations;
    return result;
}
/* Like probe_cost_batch: both clocks are outside the measured inner loop.
 * W/EMPTY share the indirect-call loop. D substitutes EMPTY, so the emitted
 * body is charged only to W; the shared call/return stays in D. */
static PROBE_NOINLINE unsigned long long probe_pl_batch(u32 kind,
    const struct probe_pl_column *c, probe_pl_emit_fn emit, u32 count, u32 phase)
{
    unsigned long long begin = probe_now();
    if (kind == PROBE_PL_Q) (void)probe_pl_wait(0, GDC_STAT_HBLANK, count + 1);
    else if (kind == PROBE_PL_TRANS || kind == PROBE_PL_JOINT) {
        struct probe_pl_stamp first = { .clocks = kind == PROBE_PL_JOINT ?
            PROBE_PL_STAMP_BEGIN | PROBE_PL_STAMP_END : PROBE_PL_STAMP_END };
        struct probe_pl_stamp next = { .clocks = PROBE_PL_STAMP_BEGIN };
        u32 iterations;
        /* Both kinds run once: a real low->high wait must fit the IF budget.
         * Joint includes real OUTs; linkage starts after the successful body
         * and runs through its return, prep, and the next wait's entry. */
        if (probe_pl_success(&c->row[phase & 1],
                kind == PROBE_PL_JOINT ? emit : probe_pl_empty,
                GDC_STAT_HBLANK, GDC_STAT_HBLANK, 1, probe_pl_edge_upper,
                0, 0, &iterations, &first)) probe_error(PROBE_ERR_PL_SUCCESS, iterations);
        if (kind == PROBE_PL_TRANS) {
            const struct PlRow *r = probe_pl_prepare(c, phase + 1);
            /* Enter and sample the next wait, without waiting a second H.
             * upper=2 executes INs and the finite-limit path, not zero-trip. */
            (void)probe_pl_success(r, probe_pl_empty,
                GDC_STAT_HBLANK, GDC_STAT_HBLANK, 1, 2, 0, 0, &iterations, &next);
        }
        unsigned long long end = probe_now();
        probe_pl_batch_total = end - begin;
        if (kind == PROBE_PL_JOINT)
            probe_pl_batch_outside = first.begin - begin + end - first.end;
        probe_pl_batch_polls = first.iterations;
        return kind == PROBE_PL_JOINT ? first.end - first.begin : next.begin - first.end;
    } else if (kind == PROBE_PL_D) {
        for (u32 i = 0; i < count; i++)
            (void)probe_pl_commit(&c->row[(i + phase) & 1], probe_pl_empty, 1, 1, 2);
    } else if (kind == PROBE_PL_PREP) {
        for (u32 i = 0; i < count; i++) (void)probe_pl_prepare(c, i + phase);
    } else {
        for (u32 i = 0; i < count; i++) {
            const struct PlRow *r = probe_pl_prepare(c, i + phase);
            emit(r);
        }
    }
    return probe_now() - begin;
}

/* H reason: 0=not run, 1=complete, 2=Q zero, 3=clock budget,
 * 4=RAM capacity, 5=open wait, 6=incomplete, 8=IF0 overrun. */
struct probe_hcal {
    unsigned long long q_before, q_after, lower_before, one, span_min, span_max;
    unsigned long long lower, upper, limit;
    u32 count;
};
static struct probe_hdiag {
    u32 reason, count, bursts, completed, rise, fall, high, low, over;
    /* reason=3: 1=capture clocks, 2=calibration headroom,
     * 3=calibration count, 4=calibration elapsed, 5=capture count>cap. */
    u32 budget_stage, cal_pass, den, cap;
    unsigned long long clocks, limit;
    long long excess;
    struct probe_hcal cal[2];
    unsigned long long min_us, max_us;
} probe_hd;
struct probe_raw_bit {
    u32 high, low, rise, fall, min[2], max[2];
};
static struct probe_raw {
    u32 both, either, freq[PROBE_RAW_VALUES];
    struct probe_raw_bit bit[2];
    unsigned long long elapsed;
} probe_raw[2];

/* IF=1 only. The gaps between blocks have no observed continuity. Counts
 * and run lengths are samples, not time; IRQ delays stay in elapsed. */
static void probe_raw_run(struct probe_raw_bit *b, u32 level, u32 run)
{
    if (!b->min[level] || run < b->min[level]) b->min[level] = run;
    if (run > b->max[level]) b->max[level] = run;
}
static void probe_raw_scan(u32 port, struct probe_raw *r)
{
    *r = (struct probe_raw){ .both = PROBE_RAW_VALUES - 1 };
    for (u32 block = 0; block < PROBE_RAW_BLOCKS; block++) {
        u32 old = 0, run[2] = {0};
        unsigned long long begin = probe_now();
        for (u32 i = 0; i < PROBE_RAW_READS; i++) {
            u32 value = inp(port);
            r->both &= value;
            r->either |= value;
            r->freq[value]++;
            for (u32 k = 0; k < 2; k++) {
                struct probe_raw_bit *b = &r->bit[k];
                u32 level = (value >> (5 + k)) & 1;
                u32 previous = (old >> (5 + k)) & 1;
                if (level) b->high++; else b->low++;
                if (i && level != previous) {
                    if (level) b->rise++; else b->fall++;
                    probe_raw_run(b, previous, run[k]);
                    run[k] = 0;
                }
                run[k]++;
            }
            old = value;
        }
        r->elapsed += probe_now() - begin;
        for (u32 k = 0; k < 2; k++)
            probe_raw_run(&r->bit[k], (old >> (5 + k)) & 1, run[k]);
    }
}

/* Timestamp sandwiches include the final clock call, not the first one.
 * Short IF=0 batches separate Q/C/W; IF=1 measurements retain IRQ pollution. */
static void probe_costs(const struct SplashRaster *s);
static void probe_classify(const struct SplashRaster *s);
static void probe_entries(const struct SplashRaster *s, long long initial_margin);
static void probe_receive(void);

void platform_splash_probe_before(void)
{
    probe_raw_before = inp(PEGC_HSYNC_PORT);
    probe_bios_before = v86_bios_real_lowmem()[PEGC_BIOS_PRXCRT];
}
static unsigned long long probe_now(void)
{
    u32 lo, hi;
    if (sys_time_now(&lo, &hi)) { probe_error(PROBE_ERR_CLOCK_READ, probe_previous); return probe_previous; }
    unsigned long long t = ((unsigned long long)hi << BITS_PER_U32) | lo;
    if (t < probe_previous) probe_error(PROBE_ERR_CLOCK_BACKWARD, probe_previous - t);
    probe_previous = t;
    return t;
}
static PROBE_NOINLINE void probe_poll_batch(u32 count)
{
    /* Identical capture work for Q calibration and H bursts. Volatile keeps
     * calibration stores even though only H bursts consume the samples. */
    for (u32 i = 0; i < count; i++)
        probe_hstatus[i] = inp(GDC_GFX_STAT);
}
static PROBE_NOINLINE unsigned long long probe_cost_batch(u32 kind, u32 count)
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
        if (probe_fatal) break;
    }
    probe_error(PROBE_ERR_OPEN_WAIT, probe_open_until);
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
    unsigned long long q_one = 0;
    probe_q_lower = 0;
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
        if (kind == PROBE_Q) q_one = elapsed;
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
            probe_error(PROBE_ERR_COST_BUDGET, count * seed[kind] + 2 * seed[PROBE_C]); return;
        }
        c->batch = count;
        c->upper_samples = count * (kind == PROBE_C ? 1 : clock_samples);
        unsigned long long shortest = ~0ULL;
        for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
            if (probe_measure_open()) return;
            unsigned int flags = irq_save();
            unsigned long long span = probe_cost_batch(kind, count);
            probe_measure_restore(flags, s);
            if (span < shortest) shortest = span;
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
        /* Difference against the one-poll sandwich removes fixed call /
         * loop entry costs. Subtract both timestamp quantization errors.
         * A zero lower bound cannot size a finite uninterrupted capture. */
        if (kind == PROBE_Q && count > 1 &&
            shortest > q_one + 2 * SPLASH_RASTER_CLOCK_QUANTUM_US)
            probe_q_lower = (shortest - q_one - 2 * SPLASH_RASTER_CLOCK_QUANTUM_US) *
                            s->line_den / (count - 1);
    }
}
static unsigned long long probe_delta(unsigned long long a, unsigned long long b)
{
    return a > b ? a - b : b - a;
}
/* Aggregate after restoring IF. The RAM buffer is one uninterrupted run. */
static u32 probe_hcollect(u32 count, u32 completed)
{
    u32 old = probe_hstatus[0], rise = 0, highs = 0, armed = 0;
    if (old & GDC_STAT_HBLANK) probe_hd.high++; else probe_hd.low++;
    for (u32 i = 1; i < count; i++) {
        u32 status = probe_hstatus[i];
        if (status & GDC_STAT_HBLANK) probe_hd.high++; else probe_hd.low++;
        if ((old & GDC_STAT_HBLANK) && !(status & GDC_STAT_HBLANK)) probe_hd.fall++;
        if (!(old & GDC_STAT_HBLANK) && (status & GDC_STAT_HBLANK)) {
            probe_hd.rise++;
            if (armed && completed < PROBE_HPERIODS) {
                probe_hsamples[completed] = i - rise;
                probe_highsamples[completed++] = highs;
            }
            rise = i; highs = 0; armed = 1;
        }
        if (armed && (status & GDC_STAT_HBLANK)) highs++;
        old = status;
    }
    return completed;
}
/* Refine Q with the identical RAM-store loop, first over <=32H, then <=64H.
 * The second pass uses the first pass's upper bound to size its batch.
 * Both remain below the 70H capture limit, including clocks.
 * The short bootstrap's two timestamp quanta are too large for a 64H count.
 * Both calibration and capture have time limits independent of that count. */
static int probe_hcalibrate(const struct SplashRaster *s, unsigned long long *q,
    unsigned long long clocks, u32 pass)
{
    struct probe_hcal *c = &probe_hd.cal[pass];
    probe_hd.cal_pass = pass + 1;
    unsigned long long limit = PROBE_HPERIODS / (pass ? 1 : 2) * s->line_num_us;
    *c = (struct probe_hcal){ .q_before = *q, .lower_before = probe_q_lower,
        .limit = limit, .span_min = ~0ULL };
    if (clocks + *q >= limit) { probe_hd.reason = 3; probe_hd.budget_stage = 2; return -1; }
    u32 count = (limit - clocks) / *q;
    if (count > PROBE_HSAMPLES) count = PROBE_HSAMPLES;
    c->count = count;
    if (count < 2) { probe_hd.reason = 3; probe_hd.budget_stage = 3; return -1; }
    /* A single short sandwich can round up by one whole quantum. Retain
     * the minimum of repeated identical sandwiches; the differential
     * below still subtracts both timestamp quanta. */
    unsigned long long one = ~0ULL;
    unsigned int flags;
    for (u32 sample = 0; sample < PROBE_COST_BATCH; sample++) {
        if (probe_measure_open()) { probe_hd.reason = 5; return -1; }
        flags = irq_save();
        unsigned long long span = probe_cost_batch(PROBE_Q, 1);
        probe_measure_restore(flags, s);
        if (span < one) one = span;
    }
    c->one = one;
    unsigned long long lower = ~0ULL, upper = 0;
    for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
        if (probe_measure_open()) { probe_hd.reason = 5; return -1; }
        flags = irq_save();
        unsigned long long span = probe_cost_batch(PROBE_Q, count);
        probe_measure_restore(flags, s);
        if (span < c->span_min) c->span_min = span;
        if (span > c->span_max) c->span_max = span;
        if ((span + probe_cost[PROBE_C].upper_us + SPLASH_RASTER_CLOCK_QUANTUM_US) *
            s->line_den > limit) { probe_hd.reason = 3; probe_hd.budget_stage = 4; return -1; }
        unsigned long long lo = span > one + 2 * SPLASH_RASTER_CLOCK_QUANTUM_US ?
            (span - one - 2 * SPLASH_RASTER_CLOCK_QUANTUM_US) * s->line_den / (count - 1) : 0;
        unsigned long long hi = ((span + SPLASH_RASTER_CLOCK_QUANTUM_US - probe_c_min) *
                                 s->line_den + count - 1) / count;
        if (lo < lower) lower = lo;
        if (hi > upper) upper = hi;
        c->lower = lower; c->upper = upper;
    }
    if (!lower) { probe_hd.reason = 2; return -1; }
    probe_q_lower = lower;
    *q = upper;
    c->q_after = *q;
    return 0;
}
/* Leave room for the two partial endpoint periods. No clock, IRQ opening,
 * or armed-state reset interrupts the 64 complete rise-to-rise periods. */
static u32 probe_hblank(const struct SplashRaster *s)
{
    unsigned long long q = probe_cost_bound(PROBE_Q, s->line_den);
    unsigned long long clocks = (2 * probe_cost[PROBE_C].upper_us +
                                 SPLASH_RASTER_CLOCK_QUANTUM_US) * s->line_den;
    unsigned long long limit = (PROBE_HPERIODS + 6) * s->line_num_us;
    probe_hd = (struct probe_hdiag){ .den = s->line_den, .clocks = clocks, .limit = limit };
    if (!q || clocks >= limit) {
        probe_hd.reason = !q ? 2 : 3;
        if (q) probe_hd.budget_stage = 1;
        probe_error(PROBE_ERR_H_BUDGET, clocks); return 0;
    }
    for (u32 pass = 0; pass < 2; pass++)
        if (probe_hcalibrate(s, &q, clocks, pass)) { probe_error(PROBE_ERR_H_CALIBRATE, pass); return 0; }
    /* At least 65H plus one sample spans 64 complete periods at any phase.
     * Refuse inadequate precision instead of enlarging the 70H IF=0 limit. */
    u32 count = ((PROBE_HPERIODS + 1) * s->line_num_us + q + probe_q_lower - 1) / probe_q_lower + 1;
    u32 cap = (limit - clocks) / q;
    probe_hd.count = count;
    probe_hd.cap = cap;
    probe_hd.excess = (long long)(count * q + clocks) - (long long)limit;
    if (count < 2 || count > PROBE_HSAMPLES) { probe_hd.reason = 4; probe_error(PROBE_ERR_H_CAPACITY, count); return 0; }
    if (count > cap) { probe_hd.reason = 3; probe_hd.budget_stage = 5; probe_error(PROBE_ERR_H_COUNT, count); return 0; }
    u32 completed = 0;
    for (u32 burst = 0; burst < 1; burst++) {
        if (probe_measure_open()) { probe_hd.reason = 5; break; }
        probe_hd.bursts++;
        unsigned int flags = irq_save();
        unsigned long long a = probe_now();
        probe_poll_batch(count);
        unsigned long long elapsed = probe_now() - a;
        probe_measure_restore(flags, s);
        completed = probe_hcollect(count, completed);
        probe_hd.completed = completed;
        if (probe_hd.bursts == 1 || elapsed < probe_hd.min_us) probe_hd.min_us = elapsed;
        if (elapsed > probe_hd.max_us) probe_hd.max_us = elapsed;
        if ((elapsed + probe_cost[PROBE_C].upper_us + SPLASH_RASTER_CLOCK_QUANTUM_US) *
            s->line_den > limit) {
            probe_hd.over++; probe_hd.reason = 8;
            probe_error(PROBE_ERR_H_OVERRUN, elapsed); return 0;
        }
    }
    if (!probe_hd.reason) probe_hd.reason = completed == PROBE_HPERIODS ? 1 : 6;
    if (completed != PROBE_HPERIODS) probe_error(PROBE_ERR_H_INCOMPLETE, completed);
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
        if (probe_fatal || after > deadline) break;
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
    probe_error(PROBE_ERR_PHASE_WAIT, after);
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
                if (probe_fatal) return;
            }
            if (poll == PROBE_POLLS) { probe_error(PROBE_ERR_ENTRY_OPEN, poll); return; }
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
    long long sample_q = probe_hd.cal[1].q_after;
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
    /* Duty ratio times H, with one sample of edge uncertainty. C is absent:
     * neither endpoint clock is in the uninterrupted IN sample runs.
     * Use the final RAM-loop calibration, not the bootstrap Q bound.
     * Use individual extrema; an average cannot hide a narrower H pulse. */
    probe_budget.b.min = 0x7fffffffffffffffLL;
    for (u32 i = 0; i < PROBE_HPERIODS; i++) {
        unsigned long long samples = probe_hsamples[i], highs = 0;
        highs += probe_highsamples[i];
        long long lo = samples ? highs * probe_budget.h.min / samples : 0;
        long long lower = lo > sample_q ? lo - sample_q : 0;
        long long upper = samples ? (highs * probe_budget.h.max + samples - 1) / samples +
                                 sample_q : 0;
        if (lower < probe_budget.b.min) probe_budget.b.min = lower;
        if (upper > probe_budget.b.max) probe_budget.b.max = upper;
    }
}
static void probe_pl_columns_init(void)
{
    static const u8 masks[PROBE_PL_MASKS] = {1, 2, 4, 3, 5, 6, 7};
    static const u8 ports[3] = {PAL_G_PORT, PAL_R_PORT, PAL_B_PORT};
    probe_pl_count = 0;
    for (u32 n = SPLASH_RASTER_IMAGE_OUTS; n <= SPLASH_RASTER_IMAGE_DIAG_OUTS; n++)
        for (u32 m = 2; m <= 3; m++)
            for (u32 a = 0; a < PROBE_PL_MASKS; a++)
                for (u32 b = 0; b < PROBE_PL_MASKS; b++)
                    for (u32 d = 0; d < (m == 3 ? PROBE_PL_MASKS : 1); d++) {
                        const u8 mask[3] = {masks[a], masks[b], masks[d]};
                        struct PlRow r = { .first = 0 };
                        u32 cost = m - 1;
                        for (u32 i = 0; i < m; i++)
                            for (u32 k = 0; k < 3; k++) cost += !!(mask[i] & (1u << k));
                        if (cost != n) continue;
                        struct probe_pl_column *c = &probe_pl_columns[probe_pl_count++];
                        *c = (struct probe_pl_column){ .n = n, .colors = m, .scan_reason = 3 };
                        for (u32 i = 0; i < m; i++) {
                            if (i) { r.port[r.count] = PAL_IDX_PORT; r.value[r.count++] = i; }
                            for (u32 k = 0; k < 3; k++) if (mask[i] & (1u << k)) {
                                r.port[r.count] = ports[k]; r.value[r.count++] = 15;
                            }
                        }
                        c->row[0] = c->row[1] = r;
                        for (u32 i = 0; i < r.count; i++)
                            if (r.port[i] != PAL_IDX_PORT) c->row[1].value[i] = 0;
                    }
}
static void probe_pl_peak_add(struct probe_pl_peak *p, long long value)
{
    if (!p->batches || value > p->max) {
        if (p->batches) { p->second = p->max; p->second_valid = 1; }
        p->max = value;
        p->hits = 1;
    } else if (value == p->max) p->hits++;
    else if (!p->second_valid || value > p->second) {
        p->second = value;
        p->second_valid = 1;
    }
    p->batches++;
}
static void probe_pl_detail_add(struct probe_pl_detail *d,
    unsigned long long span, u32 count, u32 den, u32 clocks)
{
    long long raw = ((long long)span * den + count - 1) / count;
    long long corrected = ((long long)(span + clocks * SPLASH_RASTER_CLOCK_QUANTUM_US - clocks * probe_c_min) *
                           den + count - 1) / count;
    probe_pl_peak_add(&d->raw, raw);
    probe_pl_peak_add(&d->upper, corrected);
    /* Difference of rounded bounds: signed; no assumption of cancellation. */
    probe_pl_peak_add(&d->correction, corrected - raw);
}
/* Phase setup is outside IF=0 and all timed intervals. Joint's two entry
 * clocks start at fresh high; linkage's one entry clock starts at fresh low.
 * This avoids paying for a missed low phase within the unchanged 2H cap. */
static int probe_pl_align(u32 kind)
{
    if (kind != PROBE_PL_JOINT && kind != PROBE_PL_TRANS) return 0;
    u32 first = kind == PROBE_PL_TRANS ? GDC_STAT_HBLANK : 0;
    if (!probe_pl_wait(GDC_STAT_HBLANK, first, probe_pl_edge_upper) ||
        !probe_pl_wait(GDC_STAT_HBLANK, first ^ GDC_STAT_HBLANK, probe_pl_edge_upper)) {
        probe_error(PROBE_ERR_PL_ALIGN, probe_pl_edge_upper);
        return -1;
    }
    return 0;
}
static struct SplashRasterTimeRange probe_pl_measure(const struct SplashRaster *s,
    u32 kind, struct probe_pl_column *c, probe_pl_emit_fn emit, u32 phase,
    unsigned long long *one_upper)
{
    u32 errors_before = probe_errors;
    probe_context.phase = phase; probe_context.n = c->n; probe_context.kind = kind;
    struct SplashRasterTimeRange result = { .min = 0x7fffffffffffffffLL };
    probe_pl_edge_upper = 2 * s->line_num_us / (probe_q_lower ? probe_q_lower : 1) + 1;
    if (kind == PROBE_PL_W || kind == PROBE_PL_JOINT) {
        const struct PlRow *opposite = probe_pl_prepare(c, phase ^ 1);
        emit(opposite);
        if (kind == PROBE_PL_JOINT) (void)probe_pl_prepare(c, phase);
    }
    if (probe_measure_open() || probe_pl_align(kind)) return (struct SplashRasterTimeRange){0};
    unsigned int flags = irq_save();
    unsigned long long seed = probe_pl_batch(kind, c, emit, 1, phase);
    unsigned long long seed_closed = kind == PROBE_PL_JOINT || kind == PROBE_PL_TRANS ?
        probe_pl_batch_total : seed;
    probe_measure_restore(flags, s);
    if (probe_errors != errors_before) return (struct SplashRasterTimeRange){0};
    if (kind == PROBE_PL_Q)
        probe_pl_detail_add(&probe_pl_detail[c->n - SPLASH_RASTER_IMAGE_OUTS][1],
                            seed, 1, s->line_den, 1);
    /* One joint operation per sandwich keeps first-index restoration and
     * the opposite color outside the measured IN-to-OUT interval. */
    u32 count = kind == PROBE_PL_JOINT || kind == PROBE_PL_TRANS ? 1 : PROBE_BURST;
    unsigned long long clocks = 2 * probe_cost[PROBE_C].upper_us;
    /* Eight fast INs can be shorter than seed + two timestamp quanta,
     * producing a zero lower bound. Only Q needs a longer calibration;
     * its clocks still bracket the same clock-free wait loop. */
    unsigned long long limit = (kind == PROBE_PL_Q ? PROBE_HPERIODS / 2 : 2) * s->line_num_us;
    if (kind == PROBE_PL_Q && limit > clocks * s->line_den) {
        count = (limit / s->line_den - clocks) / (seed + 1);
        if (count > PROBE_COST_BATCH) count = PROBE_COST_BATCH;
        if (!count) count = 1;
    }
    while (count > 1 && (count * (seed + 1) + clocks) * s->line_den > limit)
        count /= 2;
    if ((count * (seed + 1) + clocks) * s->line_den > limit ||
        (seed_closed + probe_cost[PROBE_C].upper_us) * s->line_den > limit) {
        probe_error(PROBE_ERR_PL_SEED_BUDGET, seed_closed);
        return (struct SplashRasterTimeRange){0};
    }
    struct probe_pl_peak *peak = &c->peak[phase][kind];
    *peak = (struct probe_pl_peak){ .count = count };
    unsigned long long shortest = ~0ULL;
    for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
        if (kind == PROBE_PL_W || kind == PROBE_PL_JOINT) {
            const struct PlRow *opposite = probe_pl_prepare(c, phase ^ 1);
            emit(opposite);
            if (kind == PROBE_PL_JOINT) (void)probe_pl_prepare(c, phase);
        }
        if (probe_measure_open() || probe_pl_align(kind)) return (struct SplashRasterTimeRange){0};
        flags = irq_save();
        unsigned long long span = probe_pl_batch(kind, c, emit, count, phase);
        probe_measure_restore(flags, s);
        if (probe_errors != errors_before) return (struct SplashRasterTimeRange){0};
        unsigned long long closed = kind == PROBE_PL_JOINT || kind == PROBE_PL_TRANS ?
            probe_pl_batch_total : span;
        if ((closed + probe_cost[PROBE_C].upper_us) * s->line_den > limit) {
            probe_error(PROBE_ERR_PL_BATCH_BUDGET, closed);
            return (struct SplashRasterTimeRange){0};
        }
        if (span < shortest) shortest = span;
        long long lo = (long long)span * s->line_den -
            probe_cost_bound(PROBE_C, s->line_den) - SPLASH_RASTER_CLOCK_QUANTUM_US * s->line_den;
        long long hi = (long long)(span + SPLASH_RASTER_CLOCK_QUANTUM_US - probe_c_min) * s->line_den;
        lo = lo > 0 ? lo / count : 0;
        hi = (hi + count - 1) / count;
        if (lo < result.min) result.min = lo;
        if (hi > result.max) result.max = hi;
        probe_pl_peak_add(peak, hi);
        if (kind == PROBE_PL_JOINT) {
            probe_pl_detail_add(&probe_pl_outside[c->n][phase],
                probe_pl_batch_outside, 1, s->line_den, 2);
            probe_pl_peak_add(&probe_pl_joint_polls[c->n][phase], probe_pl_batch_polls);
        }
        if (kind == PROBE_PL_Q || kind == PROBE_PL_PREP || kind == PROBE_PL_TRANS) {
            u32 part = kind == PROBE_PL_Q ? 0 : kind == PROBE_PL_PREP ? 2 : 3;
            probe_pl_detail_add(&probe_pl_detail[c->n - SPLASH_RASTER_IMAGE_OUTS][part],
                                span, count, s->line_den, 1);
        }
    }
    if (kind == PROBE_PL_Q) {
        /* Exclude wait entry/exit with a one-poll differential, as for the
         * raw capture. Upper includes them, conservatively, per poll. */
        result.min = count > 1 && shortest > seed + 2 * SPLASH_RASTER_CLOCK_QUANTUM_US ?
            (shortest - seed - 2 * SPLASH_RASTER_CLOCK_QUANTUM_US) * s->line_den / (count - 1) : 0;
        *one_upper = (seed + SPLASH_RASTER_CLOCK_QUANTUM_US - probe_c_min) * s->line_den;
        peak->seed = *one_upper;
        if ((long long)*one_upper > result.max) result.max = *one_upper;
    }
    return result;
}
static void probe_pl_window(struct probe_pl_result *r)
{
    const struct SplashRasterImageBudget *b = &r->budget;
    r->normal = b->h.max + b->q.max + b->error;
    r->miss = 2 * b->h.min - b->q.max - b->error;
    r->early_time = b->h.min - b->q.max - b->error;
    if (r->poll.min <= 0 || r->poll.max <= 0) return;
    r->upper = r->normal / r->poll.min + 2;
    /* low+high counts only IN iterations. Include every intervening cost
     * before rejecting a short count; its greatest elapsed time must still
     * precede the earliest normal edge. Use the longest poll, not shortest. */
    long long fixed = b->d.max + b->w.max + r->prep + r->transition;
    long long remaining = r->early_time - fixed;
    r->lower = remaining > 0 ? (remaining + r->poll.max - 1) / r->poll.max : 1;
    r->tmin = (r->upper - 1) * r->poll.min;
    r->tmax = r->upper * r->poll.max + fixed;
    r->window_ok = r->normal < r->tmin && r->tmin <= r->tmax && r->tmax < r->miss &&
                   r->lower < r->upper;
}
static void probe_pl_scan(const struct SplashRaster *s, struct probe_pl_column *c,
    const struct probe_pl_result *r)
{
    probe_pl_emit_fn emit = c->n == SPLASH_RASTER_IMAGE_OUTS ? probe_pl_emit5 : probe_pl_emit6;
    /* Unqualified trials still collect consecutive H evidence, using a
     * finite acquisition cap. They cannot turn into a qualified result. */
    u32 upper = r->window_ok ? r->upper :
        (2 * r->budget.h.max + r->poll.min - 1) / r->poll.min + 2;
    u32 lower = r->window_ok ? r->lower : 1;
    for (u32 i = 0; i < c->colors; i++) palette_set(i, 0, 0, 0);
    if (probe_measure_open()) { c->scan_reason = 4; return; }
    c->scan_reason = 1;
    const struct PlRow *row = probe_pl_prepare(c, 0);
    u32 completed = 0, early = 0, timeout = 0, high_after = 0, low_after = 0;
    unsigned int flags = irq_save();
    unsigned long long begin = probe_now();
    u32 low = probe_pl_wait(GDC_STAT_HBLANK, 0, upper);
    u32 polls = low ? probe_pl_wait(GDC_STAT_HBLANK, GDC_STAT_HBLANK, upper) : 0;
    if (!polls) timeout = 1;
    for (u32 y = 0; !timeout && y < PROBE_HPERIODS; y++) {
        if (probe_pl_commit(row, emit, polls, y ? lower : 1, upper)) { early = 1; break; }
        low = probe_pl_wait(GDC_STAT_HBLANK, 0, upper);
        if (!low) { timeout = 1; break; }
        if (low == 1) low_after++; else high_after++;
        row = probe_pl_prepare(c, y + 1);
        u32 high = probe_pl_wait(GDC_STAT_HBLANK, GDC_STAT_HBLANK, upper - low);
        if (!high) { timeout = 1; break; }
        polls = low + high;
        if (polls < lower || polls >= upper) { early = 1; break; }
        completed++;
    }
    unsigned long long elapsed = probe_now() - begin;
    probe_measure_restore(flags, s);
    c->elapsed_us = elapsed; c->completed = completed;
    c->early = early; c->timeout = timeout;
    c->high_after = high_after; c->low_after = low_after;
    c->window_ok = r->window_ok;
}
static void probe_pl_measure_all(const struct SplashRaster *s)
{
    probe_bounds(s);
    probe_pl_columns_init();
    for (u32 n = 0; n < 2; n++) for (u32 part = 0; part < PROBE_PL_PARTS; part++)
        probe_pl_detail[n][part] = (struct probe_pl_detail){0};
    for (u32 n = 0; n < PROBE_PL_SLOTS; n++) {
        probe_pl_joint[n] = (struct probe_pl_column){0};
        for (u32 phase = 0; phase < 2; phase++) {
            probe_pl_joint_reason[n][phase] = 0;
            probe_pl_outside[n][phase] = (struct probe_pl_detail){0};
            probe_pl_joint_polls[n][phase] = (struct probe_pl_peak){0};
        }
    }
    for (u32 n = 0; n < 2; n++) {
        struct probe_pl_result *r = &probe_pl_result[n];
        *r = (struct probe_pl_result){ .budget = { .den = s->line_den,
            .outs = SPLASH_RASTER_IMAGE_OUTS + n, .h = probe_budget.h, .b = probe_budget.b,
            .error = SPLASH_RASTER_CLOCK_QUANTUM_US * s->line_den },
            .poll = { .min = 0x7fffffffffffffffLL } };
    }
    for (u32 i = 0; i < probe_pl_count && !probe_fatal; i++) {
        struct probe_pl_column *c = &probe_pl_columns[i];
        struct probe_pl_result *r = &probe_pl_result[c->n - SPLASH_RASTER_IMAGE_OUTS];
        probe_pl_emit_fn emit = c->n == SPLASH_RASTER_IMAGE_OUTS ? probe_pl_emit5 : probe_pl_emit6;
        u32 errors_before = probe_errors;
        probe_context.col = i;
        r->columns++;
        c->poll.min = 0x7fffffffffffffffLL;
        long long transition = 0;
        for (u32 phase = 0; phase < 2; phase++) {
            for (u32 k = 0; k < c->colors; k++) palette_set(k, 0, 0, 0);
            struct SplashRasterTimeRange cost[PROBE_PL_KINDS];
            unsigned long long one = 0;
            for (u32 k = 0; k < PROBE_PL_KINDS; k++) {
                cost[k] = probe_pl_measure(s, k, c, k == PROBE_PL_EMPTY ? probe_pl_empty : emit,
                                          phase, &one);
                if (probe_fatal) { c->scan_reason = PROBE_SCAN_ERROR; return; }
                if (probe_errors != errors_before) goto column_error;
            }
            if (cost[PROBE_PL_Q].min > 0)
                (void)probe_pl_measure(s, PROBE_PL_TRANS, c, emit, phase, &one);
            if (probe_fatal) { c->scan_reason = PROBE_SCAN_ERROR; return; }
            if (probe_errors != errors_before) goto column_error;
            long long w = cost[PROBE_PL_W].max - cost[PROBE_PL_EMPTY].min;
            c->empty_min[phase] = cost[PROBE_PL_EMPTY].min;
            if (w > c->w) c->w = w;
            if (cost[PROBE_PL_D].max > c->d) c->d = cost[PROBE_PL_D].max;
            if (cost[PROBE_PL_PREP].max > c->prep) c->prep = cost[PROBE_PL_PREP].max;
            if (cost[PROBE_PL_Q].min < c->poll.min) c->poll.min = cost[PROBE_PL_Q].min;
            if (cost[PROBE_PL_Q].max > c->poll.max) c->poll.max = cost[PROBE_PL_Q].max;
            /* Two wait entry/return paths surround the low-phase prep. */
            if ((long long)(2 * one) > transition) transition = 2 * one;
        }
        /* Adopt a column only after both phases finish without errors. */
        if (c->w > r->budget.w.max) r->budget.w.max = c->w;
        if (c->d > r->budget.d.max) r->budget.d.max = c->d;
        if (c->prep > r->prep) r->prep = c->prep;
        if (transition > r->transition) r->transition = transition;
        if (c->poll.min < r->poll.min) r->poll.min = c->poll.min;
        if (c->poll.max > r->poll.max) r->poll.max = c->poll.max;
        continue;
column_error:
        c->scan_reason = PROBE_SCAN_ERROR;
    }
    for (u32 n = 0; n < 2; n++) {
        struct probe_pl_result *r = &probe_pl_result[n];
        r->budget.q = r->poll;
        r->budget.q.max += r->prep + r->transition;
        probe_pl_window(r);
        r->qualified = r->columns == (n ? 87u : 42u) && r->window_ok && probe_period_ok &&
            probe_wave != SPLASH_RASTER_WAVE_UNKNOWN &&
            r->budget.q.max + r->budget.d.max + r->budget.w.max + r->budget.error < r->budget.b.min &&
            r->budget.q.max < r->budget.b.min &&
            r->budget.q.max < r->budget.h.min - r->budget.b.max;
    }
    for (u32 i = 0; i < probe_pl_count && !probe_fatal; i++) {
        struct probe_pl_column *c = &probe_pl_columns[i];
        struct probe_pl_result *r = &probe_pl_result[c->n - SPLASH_RASTER_IMAGE_OUTS];
        probe_context = (struct probe_error_context){ i, PROBE_CONTEXT_NONE, c->n, PROBE_CONTEXT_NONE };
        if (c->scan_reason == PROBE_SCAN_ERROR) { r->qualified = 0; continue; }
        if (r->poll.min <= 0) { c->scan_reason = 2; r->qualified = 0; continue; }
        probe_pl_scan(s, c, r);
        if (probe_errors && !probe_fatal) c->scan_reason = PROBE_SCAN_AFTER_ERROR;
        if (c->completed != PROBE_HPERIODS || c->early || c->timeout || c->low_after)
            r->qualified = 0;
    }
    if (probe_errors)
        for (u32 n = 0; n < 2; n++) probe_pl_result[n].qualified = 0;
    static const probe_pl_emit_fn emitters[PROBE_PL_SLOTS] = {
        probe_pl_empty, probe_pl_emit1, probe_pl_emit2, probe_pl_emit3,
        probe_pl_emit4, probe_pl_emit5, probe_pl_emit6
    };
    /* A zero poll lower bound cannot size a finite physical-edge wait.
     * Leave these diagnostic distributions empty, as for the skipped scan. */
    if (probe_pl_result[0].poll.min <= 0) return;
    for (u32 n = 0; n < PROBE_PL_SLOTS && !probe_fatal; n++) {
        struct probe_pl_column *c = &probe_pl_joint[n];
        probe_context.col = PROBE_CONTEXT_NONE;
        c->n = n;
        c->row[0] = probe_pl_columns[42].row[0]; /* first N6, prefix N */
        c->row[1] = probe_pl_columns[42].row[1];
        c->row[0].count = c->row[1].count = n;
        /* The N6 prefix of length three ends in an index without a component.
         * Use a complete single-color RGB column for this representative. */
        if (n == 3) {
            c->row[0].port[2] = c->row[1].port[2] = PAL_B_PORT;
            c->row[0].value[2] = 15;
            c->row[1].value[2] = 0;
        }
        for (u32 phase = 0; phase < 2; phase++) {
            for (u32 k = 0; k < 3; k++) palette_set(k, 0, 0, 0);
            /* Opposite values and the first index are outside the span. */
            emitters[n](probe_pl_prepare(c, phase ^ 1));
            (void)probe_pl_prepare(c, phase);
            unsigned long long one = 0;
            u32 errors_before = probe_errors;
            (void)probe_pl_measure(s, PROBE_PL_JOINT, c, emitters[n], phase, &one);
            probe_pl_joint_reason[n][phase] = probe_errors != errors_before ? PROBE_SCAN_ERROR :
                probe_errors ? PROBE_SCAN_AFTER_ERROR : PROBE_SCAN_COMPLETE;
            if (probe_fatal) return;
        }
    }
    probe_context = (struct probe_error_context){ PROBE_CONTEXT_NONE,
        PROBE_CONTEXT_NONE, PROBE_CONTEXT_NONE, PROBE_CONTEXT_NONE };
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
            if (probe_fatal) return;
        }
        if (now < e->planned_us) { probe_error(PROBE_ERR_ENTRY_WAIT, e->planned_us - now); return; }
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
        if (probe_fatal || *stamp > deadline) break;
        if (!!(status & bit) == high) return 0;
    }
    probe_error(PROBE_ERR_BIT_WAIT, *stamp);
    return -1;
}
static void probe_field(const char *key, unsigned long long n)
{
    /* All timestamps are absolute; print high:low decimal words losslessly. */
    serial_puts_polled(key);
    if (n >> BITS_PER_U32) { serial_diag_u32(n >> BITS_PER_U32); serial_puts_polled(":"); }
    serial_diag_u32(n);
}
static void probe_signed(const char *key, long long n);
/* At most 42 additional lines: H + budget + 2 calibrations +
 * 2*(summary + 2 bits + 16 hist).
 * Histograms retain every nonzero byte value, packed 16 pairs per line. */
static void probe_measure_dump(void)
{
    probe_field("SPLR h_reason=", probe_hd.reason);
    probe_field(" count=", probe_hd.count); probe_field(" bursts=", probe_hd.bursts);
    probe_field(" completed=", probe_hd.completed); probe_field(" rise=", probe_hd.rise);
    probe_field(" fall=", probe_hd.fall); probe_field(" high=", probe_hd.high);
    probe_field(" low=", probe_hd.low); probe_field(" min_us=", probe_hd.min_us);
    probe_field(" max_us=", probe_hd.max_us); probe_field(" if0_over=", probe_hd.over);
    serial_puts_polled("\n");
    probe_field("SPLR h_budget_stage=", probe_hd.budget_stage);
    probe_field(" cal_pass=", probe_hd.cal_pass); probe_field(" den=", probe_hd.den);
    probe_field(" clocks_num=", probe_hd.clocks); probe_field(" limit_num=", probe_hd.limit);
    probe_field(" count=", probe_hd.count); probe_field(" cap=", probe_hd.cap);
    probe_signed(" excess_num=", probe_hd.excess); serial_puts_polled("\n");
    for (u32 i = 0; i < 2; i++) {
        const struct probe_hcal *c = &probe_hd.cal[i];
        probe_field("SPLR h_cal=", i + 1); probe_field(" den=", probe_hd.den);
        probe_field(" q_before_num=", c->q_before); probe_field(" q_after_num=", c->q_after);
        probe_field(" lower_before_num=", c->lower_before); probe_field(" one_us=", c->one);
        probe_field(" count=", c->count); probe_field(" span_min_us=", c->span_min);
        probe_field(" span_max_us=", c->span_max); probe_field(" lower_num=", c->lower);
        probe_field(" upper_num=", c->upper); probe_field(" limit_num=", c->limit);
        serial_puts_polled("\n");
    }
    for (u32 p = 0; p < 2; p++) {
        const struct probe_raw *r = &probe_raw[p];
        u32 port = p ? GDC_TEXT_STAT : GDC_GFX_STAT;
        probe_field("SPLR raw_port=", port);
        probe_field(" blocks=", PROBE_RAW_BLOCKS); probe_field(" reads=", PROBE_RAW_READS);
        probe_field(" and=", r->both); probe_field(" or=", r->either);
        probe_field(" elapsed_us=", r->elapsed); serial_puts_polled("\n");
        for (u32 k = 0; k < 2; k++) {
            const struct probe_raw_bit *b = &r->bit[k];
            probe_field("SPLR raw_port=", port); probe_field(" bit=", 5 + k);
            probe_field(" high=", b->high); probe_field(" low=", b->low);
            probe_field(" rise=", b->rise); probe_field(" fall=", b->fall);
            probe_field(" low_min=", b->min[0]); probe_field(" low_max=", b->max[0]);
            probe_field(" high_min=", b->min[1]); probe_field(" high_max=", b->max[1]);
            serial_puts_polled("\n");
        }
        u32 pairs = 0;
        for (u32 value = 0; value < PROBE_RAW_VALUES; value++) if (r->freq[value]) {
            if (!(pairs % PROBE_RAW_PER_LINE)) {
                probe_field("SPLR raw_hist=", port);
                serial_puts_polled(" value:count");
            }
            probe_field(" ", value); probe_field(":", r->freq[value]);
            if (!(++pairs % PROBE_RAW_PER_LINE)) serial_puts_polled("\n");
        }
        if (pairs % PROBE_RAW_PER_LINE) serial_puts_polled("\n");
    }
}
static void probe_signed(const char *key, long long n)
{
    serial_puts_polled(key);
    if (n < 0) serial_puts_polled("-");
    probe_field("", n < 0 ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n);
}
static void probe_pl_peak_fields(const char *prefix, const struct probe_pl_peak *peak)
{
    serial_puts_polled(prefix); probe_signed("max_num=", peak->batches ? peak->max : 0);
    serial_puts_polled(prefix); probe_field("max_count=", peak->hits);
    serial_puts_polled(prefix); probe_signed("second_num=", peak->second_valid ? peak->second : 0);
    serial_puts_polled(prefix); probe_field("second_valid=", peak->second_valid);
    serial_puts_polled(prefix); probe_field("samples=", peak->batches);
}
static void probe_pl_details_dump(void)
{
    for (u32 n = 0; n < 2; n++) {
        const struct probe_pl_result *r = &probe_pl_result[n];
        probe_field("SPLR pl_q_contract=", SPLASH_RASTER_IMAGE_OUTS + n);
        probe_field(" den=", r->budget.den);
        probe_signed(" poll_or_seed_num=", r->poll.max);
        probe_signed(" prep_num=", r->prep);
        probe_signed(" two_seed_num=", r->transition);
        probe_signed(" total_num=", r->budget.q.max);
        /* Linkage contains prep, so compare to prep+two_seed, not two_seed
         * alone. A negative gap is diagnostic; adoption Q is unchanged. */
        probe_signed(" link_budget_num=", r->prep + r->transition);
        probe_signed(" link_upper_num=", probe_pl_detail[n][3].upper.max);
        probe_signed(" link_gap_num=", r->prep + r->transition - probe_pl_detail[n][3].upper.max);
        serial_puts_polled("\n");
        for (u32 part = 0; part < PROBE_PL_PARTS; part++) {
            const struct probe_pl_detail *d = &probe_pl_detail[n][part];
            probe_field("SPLR pl_q_part=", part);
            probe_field(" n=", SPLASH_RASTER_IMAGE_OUTS + n);
            probe_field(" den=", r->budget.den);
            probe_pl_peak_fields(" raw_", &d->raw);
            probe_pl_peak_fields(" upper_", &d->upper);
            probe_pl_peak_fields(" clock_delta_", &d->correction);
            serial_puts_polled("\n");
        }
    }
    for (u32 n = 0; n < PROBE_PL_SLOTS; n++) for (u32 phase = 0; phase < 2; phase++) {
        const struct probe_pl_peak *p = &probe_pl_joint[n].peak[phase][PROBE_PL_JOINT];
        probe_field("SPLR pl_joint_n=", n); probe_field(" phase=", phase);
        probe_field(" scan_reason=", probe_pl_joint_reason[n][phase]);
        probe_field(" den=", probe_pl_result[0].budget.den);
        probe_field(" count=", p->count);
        probe_pl_peak_fields(" ", p);
        probe_field(" subtract_num=", 0);
        probe_pl_peak_fields(" polls_", &probe_pl_joint_polls[n][phase]);
        serial_puts_polled("\n");
        const struct probe_pl_detail *d = &probe_pl_outside[n][phase];
        probe_field("SPLR pl_joint_outer_n=", n); probe_field(" phase=", phase);
        probe_field(" scan_reason=", probe_pl_joint_reason[n][phase]);
        probe_field(" den=", probe_pl_result[0].budget.den);
        probe_pl_peak_fields(" raw_", &d->raw);
        probe_pl_peak_fields(" upper_", &d->upper);
        probe_pl_peak_fields(" clock_delta_", &d->correction);
        serial_puts_polled("\n");
    }
}
/* Dump original sample order before probe_stats sorts its separate arrays.
 * Four high:total pairs per line preserve every completed B period cheaply. */
static void probe_hsamples_dump(void)
{
    struct probe_pl_peak highs = {0};
    for (u32 i = 0; i < probe_hd.completed; i++) {
        if (!(i % PROBE_H_PER_LINE)) probe_field("SPLR b_samples=", i);
        probe_field(" ", probe_highsamples[i]); probe_field(":", probe_hsamples[i]);
        probe_pl_peak_add(&highs, probe_highsamples[i]);
        if ((i + 1) % PROBE_H_PER_LINE == 0 || i + 1 == probe_hd.completed)
            serial_puts_polled("\n");
    }
    serial_puts_polled("SPLR b_high_samples");
    probe_pl_peak_fields(" ", &highs);
    serial_puts_polled("\n");
}
static void probe_pl_dump(void)
{
    for (u32 n = 0; n < 2; n++) {
        const struct probe_pl_result *r = &probe_pl_result[n];
        const struct SplashRasterImageBudget *b = &r->budget;
        probe_field("SPLR pl_trial_n=", b->outs);
        probe_field(" columns=", r->columns); probe_field(" periods=", PROBE_HPERIODS);
        probe_field(" den=", b->den); /* all *_num values below are 1/den us */
        probe_signed(" q_num=", b->q.max); probe_signed(" d_num=", b->d.max);
        probe_signed(" w_num=", b->w.max); probe_signed(" e_num=", b->error);
        probe_signed(" h_min_num=", b->h.min); probe_signed(" h_max_num=", b->h.max);
        probe_signed(" b_min_num=", b->b.min); probe_signed(" b_max_num=", b->b.max);
        probe_signed(" prep_num=", r->prep);
        probe_signed(" h_margin_num=", b->b.min - b->q.max - b->d.max - b->w.max - b->error);
        probe_field(" provisional=", r->qualified); serial_puts_polled("\n");
        probe_field("SPLR pl_window_n=", b->outs); probe_field(" den=", b->den);
        probe_signed(" poll_min_num=", r->poll.min); probe_signed(" poll_max_num=", r->poll.max);
        probe_signed(" transition_num=", r->transition);
        probe_field(" lower=", r->lower); probe_field(" upper=", r->upper);
        probe_signed(" early_num=", r->early_time); probe_signed(" normal_num=", r->normal);
        probe_signed(" tmin_num=", r->tmin); probe_signed(" tmax_num=", r->tmax);
        probe_signed(" miss_num=", r->miss); probe_field(" window_ok=", r->window_ok);
        serial_puts_polled("\n");
    }
    for (u32 i = 0; i < probe_pl_count; i++) {
        const struct probe_pl_column *c = &probe_pl_columns[i];
        probe_field("SPLR pl_column=", i); probe_field(" n=", c->n);
        probe_field(" m=", c->colors); serial_puts_polled(" ports=");
        for (u32 k = 0; k < c->row[0].count; k++) {
            if (k) serial_puts_polled(":");
            probe_field("", c->row[0].port[k]);
        }
        probe_field(" den=", probe_pl_result[c->n - SPLASH_RASTER_IMAGE_OUTS].budget.den);
        probe_signed(" poll_min_num=", c->poll.min); probe_signed(" poll_max_num=", c->poll.max);
        probe_signed(" d_num=", c->d); probe_signed(" w_num=", c->w);
        probe_signed(" prep_num=", c->prep); probe_field(" completed=", c->completed);
        probe_field(" early=", c->early); probe_field(" timeout=", c->timeout);
        probe_field(" high_after=", c->high_after); probe_field(" low_after=", c->low_after);
        probe_field(" window_ok=", c->window_ok); probe_field(" elapsed_us=", c->elapsed_us);
        /* 1=ran, 2=zero Q lower, 3=not measured, 4=open wait,
         * 5=column measurement error, 6=continued after error. */
        probe_field(" scan_reason=", c->scan_reason);
        serial_puts_polled("\n");
        /* Q is the poll batch (seed shown separately); D/W use the same
         * corrections as d_num/w_num. W subtracts this phase's EMPTY min.
         * kind: 0=Q/poll, 1=D, 2=W, 3=EMPTY, 4=PREP. */
        for (u32 phase = 0; phase < 2; phase++) for (u32 k = 0; k < PROBE_PL_KINDS; k++) {
            const struct probe_pl_peak *p = &c->peak[phase][k];
            long long subtract = k == PROBE_PL_W ? c->empty_min[phase] : 0;
            probe_field("SPLR pl_peak=", i); probe_field(" phase=", phase); probe_field(" kind=", k);
            probe_field(" den=", probe_pl_result[c->n - SPLASH_RASTER_IMAGE_OUTS].budget.den);
            probe_field(" count=", p->count); probe_field(" batches=", p->batches);
            probe_signed(" max_num=", p->batches ? p->max - subtract : 0);
            probe_field(" max_count=", p->hits);
            probe_signed(" second_num=", p->second_valid ? p->second - subtract : 0);
            probe_field(" second_valid=", p->second_valid);
            probe_field(" seed_num=", p->seed); probe_signed(" subtract_num=", subtract);
            serial_puts_polled("\n");
        }
    }
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
    u32 bios_after = v86_bios_real_lowmem()[PEGC_BIOS_PRXCRT];
    u32 mode, baud, fifo, oe0, fe0, pe0, ov0, oe, fe, pe, ov;
    u32 clamp0 = ktime_clamp_count, tick0 = tick_count, ticks, clamps;
    unsigned long long stamp, io[3][3] = {{0}}, empty[3] = {0}, initial_begin = 0, initial_end = 0, initial_fall = 0;
    long long initial_margin = -1;
    u32 initial_status = 0, completed = 0, hcompleted = 0, missed_h = 0;
    const u32 counts[3] = {1, 4, 8};
    if (serial_gate_active() || !_irq_enabled()) return;
    serial_init(SYS_SERIAL_BAUD);
    serial_get_status(&mode, &baud, &fifo);
    serial_diag_get(&oe0, &fe0, &pe0, &ov0);
    probe_error_reset();
    probe_rx_count = probe_rx_missing = probe_rx_bytes = probe_rx_value = probe_rx_next = 0;
    probe_previous = 0;
    probe_hd = (struct probe_hdiag){0};
    probe_pl_count = 0;
    for (u32 n = 0; n < 2; n++)
        probe_pl_result[n] = (struct probe_pl_result){ .budget = {
            .den = s->line_den, .outs = SPLASH_RASTER_IMAGE_OUTS + n } };
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
        if (probe_fatal || stamp > edge_deadline) break;
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
        !probe_edges[PROBE_FRAMES].fall || !probe_edges[PROBE_FRAMES].text_fall) probe_error(PROBE_ERR_V_INCOMPLETE, completed);
    if (probe_fatal) goto dump;
    probe_classify(s);
    hcompleted = probe_hblank(s);
    if (probe_fatal) goto dump;
    probe_pl_measure_all(s);
    if (probe_fatal) goto dump;
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
        if (rc || d->overflow || d->clock_errors) { probe_error(PROBE_ERR_TABLE_DIAG, rc); break; }
        probe_receive();
    }
dump:
    /* Both independent raw scans also run after an early H failure. No
     * serial output or palette writes until both buffers are complete. */
    probe_raw_scan(GDC_GFX_STAT, &probe_raw[0]);
    probe_raw_scan(GDC_TEXT_STAT, &probe_raw[1]);
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
    probe_measure_dump();
    probe_pl_details_dump();
    probe_hsamples_dump();
    probe_pl_dump();
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
    if (probe_errors) {
        probe_field("SPLR error_first reason=", probe_error_first.reason);
        probe_field(" col=", probe_error_first.context.col);
        probe_field(" phase=", probe_error_first.context.phase);
        probe_field(" n=", probe_error_first.context.n);
        probe_field(" kind=", probe_error_first.context.kind);
        probe_field(" value=", probe_error_first.value);
        serial_puts_polled("\n");
    }
    probe_field("SPLR errors=", probe_errors);
    for (u32 reason = 1; reason < PROBE_ERR_COUNT; reason++) {
        probe_field(" reason_", reason);
        probe_field("=", probe_error_counts[reason]);
    }
    probe_field(" missing_h=", missed_h);
    probe_field(" clamps=", clamps); probe_field(" ticks=", ticks);
    probe_field(" oe=", oe - oe0); probe_field(" fe=", fe - fe0); probe_field(" pe=", pe - pe0);
    probe_field(" overflow=", ov - ov0); probe_field(" rx_words=", probe_rx_count);
    probe_field(" rx_discontinuities=", probe_rx_missing); probe_field(" rx_partial=", probe_rx_bytes);
    serial_puts_polled("\n");
}
#endif
