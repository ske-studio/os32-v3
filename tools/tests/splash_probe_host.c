/* Independent scan time: IN samples before returning, clock and each OUT
 * advance separately. No dependency on the LC-B player's host model. */
#include "types.h"
#include "pc98.h"
#include "pegc.h"
#include "gfx_hal.h"
#include "bootinfo.h"
#include "idt.h"
#include "paging.h"
#include "v86_bios.h"
#define IO_H
static unsigned long long scan_us, irq_at[4], irq_delay[4];
static u32 in_us = 1, clock_cost_us = 1, out_us = 1, calls, writes;
/* Sub-microsecond IN timing with a 1us-quantized public clock. */
static u32 in_milli, scan_milli, poll_entry_us;
static u32 pl_script, pl_reads, success_entry_cost, success_return_cost;
static int success_scanning;
static int capture_active(void);
static int pl_fail_active(void);
static unsigned long long probe_host_error_span(unsigned long long span, int seed);
enum pl_error_fixture_kind {
    PL_ERROR_NONE, PL_ERROR_ALIGN, PL_ERROR_SUCCESS, PL_ERROR_PHASE1,
    PL_ERROR_JOINT_PHASE0, PL_ERROR_JOINT_PHASE1
};
static u32 pl_error_fixture;
static u32 den, total, high_lines, raw_mode, wide_h;
static int irq_on = 1, missing_h, frozen, backwards;
static u32 initial_irq_us;
static u32 calibration_irq_us;
static u32 if0_end_clock, if0_in_reads, if0_max_in_run;
static int require_clockless_in, slow_capture;
static int raw_test, h_test;
static u32 raw_reads, raw_port, h_reads, h_high, h_rise, h_fall, h_old;
static u32 raw_clocks;
static unsigned long long test_open_at, test_min_open, test_closed_at, test_max_closed;
static void finish(int code)
{
    if (code) {
        char text[] = "line=0000\n";
        for (int i = 0; i < 4; i++) text[5+i] = "0123456789abcdef"[(code >> (12-i*4)) & 15];
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(text), "d"(sizeof(text)-1) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code ? code % 250 + 1 : 0) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(c) do { if (!(c)) finish(__LINE__); } while (0)
static void advance(u32 cost)
{
    if (++calls > 32000000) finish(__LINE__);
    if (!frozen) scan_us += cost;
    for (u32 i = 0; i < 4; i++) if (irq_on && irq_delay[i] && scan_us >= irq_at[i]) {
        scan_us += irq_delay[i]; irq_delay[i] = 0;
    }
}
int sys_time_now(u32 *lo, u32 *hi)
{
    REQUIRE(!success_scanning); /* clocks must remain outside the edge loop */
    if (raw_test) {
        REQUIRE(irq_on && !(raw_reads % 4096));
        REQUIRE(raw_clocks < 16);
        raw_clocks++;
    }
    if (!irq_on && if0_in_reads && require_clockless_in) if0_end_clock++;
    advance(clock_cost_us);
    unsigned long long t = backwards ? 10000000 - scan_us : scan_us;
    *lo = t; *hi = t >> BITS_PER_U32;
    return 0;
}
static unsigned int inp(unsigned int port)
{
    u32 status = 0;
    if (raw_test) {
        REQUIRE(irq_on && port == raw_port);
        REQUIRE(raw_clocks == 2 * (raw_reads / 4096) + 1);
        status = raw_reads++ % 256;
        if (port == GDC_TEXT_STAT) status = 255 - status;
    } else if (port == PEGC_HSYNC_PORT) status = raw_mode;
    else {
        REQUIRE(port == GDC_GFX_STAT || port == GDC_TEXT_STAT);
        unsigned long long phase = scan_us * den % ((unsigned long long)total * 100000);
        if (phase < (unsigned long long)high_lines * 100000) status |= GDC_STAT_VSYNC;
#ifdef PROBE_MODEL_PHASE
        phase = PROBE_MODEL_PHASE(scan_us);
#endif
        u32 width __attribute__((unused)) = wide_h ? (raw_mode ? 80000 : 75472) : (raw_mode ? 20000 : 24532);
#ifdef PROBE_MODEL_PHASE
        if (phase % 318 >= 318 - 59) status |= GDC_STAT_HBLANK;
#else
        if (!missing_h && phase % 100000 >= 100000 - width) status |= GDC_STAT_HBLANK;
#endif
    }
    if (pl_script) {
        /* Start high: first-IN forced success must fail this fixture.
         * The real predicate sees high, low, low, high in that order. */
        status = (++pl_reads % 4 == 1 || pl_reads % 4 == 0) ? GDC_STAT_HBLANK : 0;
    }
    if (pl_fail_active()) status = 0;
    if (h_test && !irq_on) {
        if (!(if0_in_reads)) h_old = status;
        else {
            if (!(h_old & GDC_STAT_HBLANK) && (status & GDC_STAT_HBLANK)) h_rise++;
            if ((h_old & GDC_STAT_HBLANK) && !(status & GDC_STAT_HBLANK)) h_fall++;
        }
        h_reads++; h_high += !!(status & GDC_STAT_HBLANK); h_old = status;
    }
    if (!irq_on && !if0_in_reads) advance(poll_entry_us);
    scan_milli += in_milli;
    advance(in_us + scan_milli / 1000 +
            (slow_capture && capture_active() && !irq_on && if0_in_reads >= 700));
    scan_milli %= 1000;
    /* Sample and return are deliberately distinct. */
    if (!irq_on) {
        if (require_clockless_in && !success_scanning) REQUIRE(!if0_end_clock);
        if0_in_reads++;
        if (if0_in_reads > if0_max_in_run) if0_max_in_run = if0_in_reads;
    }
    return status;
}
static void outp(unsigned int port, unsigned int value)
{
    REQUIRE(port == PAL_IDX_PORT || port == PAL_G_PORT || port == PAL_R_PORT || port == PAL_B_PORT);
    REQUIRE(value < 16);
    writes++; advance(out_us);
    if (irq_on && calibration_irq_us) {
        advance(calibration_irq_us); calibration_irq_us = 0;
    }
}
static int _irq_enabled(void) { return irq_on; }
static unsigned int irq_save(void)
{
    u32 flags = irq_on;
    if (test_open_at && scan_us - test_open_at < test_min_open) test_min_open = scan_us - test_open_at;
    irq_on = 0; test_closed_at = scan_us;
    if0_in_reads = if0_end_clock = 0;
    if (h_test) h_reads = h_high = h_rise = h_fall = 0;
    return flags;
}
static void irq_restore(unsigned int flags)
{
    if (scan_us - test_closed_at > test_max_closed) test_max_closed = scan_us - test_closed_at;
    irq_on = flags; test_open_at = scan_us;
}
#include "../../drivers/video/pc98/splash_raster.c"
static u8 probe_bios_snapshot[PAGE_SIZE];
static u32 probe_bios_reads;
const u8 *v86_bios_real_lowmem(void)
{
    probe_bios_reads++;
    return probe_bios_snapshot;
}
static inline volatile void *probe_io_address(u32 address)
{
    REQUIRE(address >= PAGE_SIZE); /* The kernel leaves page0 not present. */
    return P2V_IO(address);
}
#define P2V_IO(address) probe_io_address(address)
static int probe_frame_checked(struct SplashRaster *s, const GFX_RasterPalTable *t);
#define pc98_splash_raster_table probe_frame_checked
/* The host build inserts this guard at the aggregation entry. */
static void probe_hcollect_check(void) { REQUIRE(irq_on); }
static u32 fixture_q, fixture_lower;
static int probe_host_hcalibrate(const struct SplashRaster *, unsigned long long *,
    unsigned long long, u32);
/* Inject only into returned batch spans, after the actual clock-free batch
 * and IF restoration. The seed is deliberately outside this fixture. */
static u32 peak_fixture, peak_index;
static void __attribute__((unused)) probe_host_success_enter(void)
{
    advance(success_entry_cost);
}
static void __attribute__((unused)) probe_host_success_return(void)
{
    advance(success_return_cost);
}
static unsigned long long __attribute__((unused)) probe_host_pl_span(unsigned long long span)
{
    REQUIRE(irq_on);
    if (!peak_fixture) return span;
    u32 i = peak_index++;
    REQUIRE(i < 64);
    if (peak_fixture == 2) return 8;
    return i == 7 || i == 51 ? 12 : i == 23 ? 10 : 8;
}
#include "../../platform/pc98/splash_probe.c"
#undef pc98_splash_raster_table
static int pl_fail_active(void)
{
    if (pl_error_fixture >= PL_ERROR_JOINT_PHASE0) {
        return probe_context.col == PROBE_CONTEXT_NONE && probe_context.n == 2 &&
            probe_context.phase == pl_error_fixture - PL_ERROR_JOINT_PHASE0 && probe_context.kind == PROBE_PL_JOINT &&
            probe_pl_joint[2].peak[pl_error_fixture - PL_ERROR_JOINT_PHASE0][PROBE_PL_JOINT].batches == 3;
    }
    if (probe_context.col != 4 || probe_context.phase != (pl_error_fixture == PL_ERROR_PHASE1 ? 1u : 0u) ||
        probe_context.kind != PROBE_PL_TRANS) return 0;
    return pl_error_fixture == PL_ERROR_ALIGN || pl_error_fixture == PL_ERROR_PHASE1 || (pl_error_fixture == PL_ERROR_SUCCESS && !irq_on);
}
static unsigned long long __attribute__((unused)) probe_host_error_span(unsigned long long span, int seed)
{
    if (pl_error_fixture != PL_ERROR_PHASE1 || probe_context.col != 4 || probe_context.phase != 0) return span;
    if (seed) return span + (probe_context.kind == PROBE_PL_Q ? 4 : 0);
    if (probe_context.kind == PROBE_PL_W || probe_context.kind == PROBE_PL_D ||
        probe_context.kind == PROBE_PL_PREP) return span + 8;
    return span;
}
static int capture_active(void) { return probe_hd.bursts != 0; }
/* Supply measured calibration bounds to the real capture admission path.
 * All other cases still execute both product calibration passes. */
/* The independent waveform harness includes this file without this hook. */
static int __attribute__((unused)) probe_host_hcalibrate(const struct SplashRaster *s, unsigned long long *q,
    unsigned long long clocks, u32 pass)
{
    if (!fixture_q) return probe_hcalibrate(s, q, clocks, pass);
    *q = fixture_q;
    probe_q_lower = fixture_lower;
    return 0;
}

volatile u32 tick_count;
u32 ktime_clamp_count;
const char os32_build_commit[] = "host-probe";
static struct bootinfo probe_bootinfo = { .img_crc = 123, .source = 2 };
static struct pit_setup probe_pit = { .reload = 24576 };
static int probe_presents, probe_rects, probe_rx_pos;
static u32 probe_checked_frames;
static char probe_output[524288];
static u32 probe_output_size, probe_serial_baud;
int gfx_current_height = 400, gfx_flip_enabled = 1;
const struct bootinfo *bootinfo_get(void) { return &probe_bootinfo; }
const struct pit_setup *pit_get_setup(void) { return &probe_pit; }
int serial_gate_active(void) { return 0; }
void serial_init(unsigned long baud)
{
    REQUIRE(baud == SYS_SERIAL_BAUD && irq_on);
    REQUIRE(!probe_output_size);
    probe_serial_baud = baud;
}
void serial_puts_polled(const char *p)
{
    REQUIRE(probe_serial_baud == SYS_SERIAL_BAUD);
    REQUIRE(probe_frames[PROBE_FRAMES-1].duration || probe_errors);
    REQUIRE(irq_on);
    while (*p) { REQUIRE(probe_output_size < sizeof(probe_output)-1); probe_output[probe_output_size++] = *p++; }
    probe_output[probe_output_size] = 0;
}
int serial_get_status(u32 *mode, u32 *baud, u32 *fifo)
{
    REQUIRE(probe_serial_baud == SYS_SERIAL_BAUD);
    *mode = 0; *baud = probe_serial_baud; *fifo = 1; return 0;
}
void serial_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{ *oe = *fe = *pe = *overflow = 0; }
int serial_trygetchar(void)
{
    if (probe_rx_pos >= 8 * 4) return -1;
    int i = probe_rx_pos++;
    return i % 4 ? 0 : i / 4;
}
void platform_splash_clear(u8 c) { REQUIRE(c == 0); }
void platform_splash_fill_rect(int x, int y, int w, int h, u8 c)
{
    REQUIRE(x >= 0 && y >= 0 && w > 0 && h > 0 && x+w <= 640 && y+h <= 400 && c < 16);
    probe_rects++;
}
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ REQUIRE(x == 0 && y == 0 && w == 640 && h == 400); }
void gfx_present_nosync(void) { probe_presents++; }
void palette_set(int index, u8 r, u8 g, u8 b)
{
    color(index, r, g, b);
    /* Inject into the initial sixteen colors, after PL calibration resets. */
    if (index == 0 && initial_irq_us && probe_pl_count &&
        probe_pl_columns[probe_pl_count-1].completed == PROBE_HPERIODS &&
        probe_pl_joint[6].peak[1][PROBE_PL_JOINT].batches == PROBE_COST_BATCH) {
        advance(initial_irq_us); initial_irq_us = 0;
    }
}
static int probe_contains(const char *s)
{
    for (u32 i = 0; i < probe_output_size; i++) {
        u32 j = 0;
        while (s[j] && i+j < probe_output_size && s[j] == probe_output[i+j]) j++;
        if (!s[j]) return 1;
    }
    return 0;
}
static int probe_frame_checked(struct SplashRaster *s, const GFX_RasterPalTable *t)
{
    int first = probe_checked_frames < 9 ? 8 + (int)probe_checked_frames - 4 : 4;
    REQUIRE(t->entries[1].line == first);
    REQUIRE(t->entries[0].r == 15 && t->entries[0].g == 0);
    REQUIRE(t->entries[1].r == 0 && t->entries[1].g == 15);
    for (int i = 2; i < t->count; i++) {
        REQUIRE(t->entries[i].line == t->entries[i-1].line + 4);
        REQUIRE(t->entries[i].r != t->entries[i-1].r);
    }
    if (probe_checked_frames >= 9) REQUIRE(t->entries[t->count-1].line == 396);
    probe_checked_frames++;
    return pc98_splash_raster_table(s, t);
}
static void setup(int mode, int vs)
{
    pl_error_fixture = 0;
    probe_hd = (struct probe_hdiag){0};
    peak_fixture = peak_index = 0;
    pl_script = pl_reads = success_entry_cost = success_return_cost = success_scanning = 0;
    raw_test = h_test = 0;
    raw_reads = raw_clocks = h_reads = h_high = h_rise = h_fall = 0;
    scan_us = 0; den = mode ? 3147 : 2483; total = mode ? 449 : 440;
    in_milli = scan_milli = poll_entry_us = 0;
    high_lines = vs ? (mode ? 2 : 8) : total - 400;
    raw_mode = mode; wide_h = 0;
    irq_on = 1; missing_h = frozen = backwards = 0;
    calls = writes = 0; probe_error_reset(); probe_previous = 0;
    probe_output_size = probe_presents = probe_rects = probe_rx_pos = 0;
    probe_bios_reads = probe_serial_baud = 0;
    probe_bios_snapshot[PEGC_BIOS_PRXCRT] = mode ? PEGC_BIOS_PRXCRT_31KHZ : 0;
    probe_checked_frames = 0;
    initial_irq_us = 0;
    calibration_irq_us = 0;
    if0_end_clock = if0_in_reads = if0_max_in_run = 0;
    require_clockless_in = 0;
    slow_capture = 0;
    test_open_at = test_closed_at = test_max_closed = 0; test_min_open = ~0ULL;
    for (u32 i = 0; i < 4; i++) irq_at[i] = irq_delay[i] = 0;
    pc98_splash_raster_reset();
}
static void costs(void)
{
    const u32 costs[] = {1, 5, 8};
    for (u32 i = 0; i < 3; i++) for (u32 c = 0; c < 3; c++) for (u32 o = 1; o <= 2; o++) {
        setup(1, 0); in_us = costs[i]; clock_cost_us = costs[c]; out_us = o;
        probe_costs(pc98_splash_raster_state());
        const u32 expected[] = {in_us, clock_cost_us, 4 * out_us};
        for (u32 kind = 0; kind < PROBE_COST_KINDS; kind++) {
            const struct SplashRasterProbeCost *p = &probe_cost[kind];
            REQUIRE(p->total_us == p->samples * expected[kind]);
            REQUIRE(p->batch > 1 && p->batch <= 8);
            REQUIRE(p->upper_num_us == p->upper_samples * expected[kind] +
                    (kind == PROBE_C ? 1 : 1 + probe_cost[PROBE_C].batch));
            REQUIRE(probe_cost_bound(kind, den) ==
                    (p->upper_num_us * den + p->upper_samples - 1) / p->upper_samples);
            REQUIRE(p->open_total_us == p->total_us);
        }
        REQUIRE(test_max_closed * den <= 200000);
        REQUIRE(test_min_open * den >= 100000);
        REQUIRE(irq_on && !probe_errors);
    }
    in_us = clock_cost_us = out_us = 1;
    setup(1, 0); calibration_irq_us = 100;
    probe_costs(pc98_splash_raster_state());
    REQUIRE(probe_cost[PROBE_W].upper_num_us == 265);
    REQUIRE(probe_cost[PROBE_W].open_max_us == 132);
    REQUIRE(probe_cost[PROBE_W].open_total_us == probe_cost[PROBE_W].total_us + 100);
    REQUIRE(test_max_closed * den <= 200000 && irq_on);
}
static void budgets(void)
{
    struct SplashRasterEntryBudget b = { .s = {{10,20},{40,50}}, .planned = 10, .retry = 3, .usable = 3 };
    REQUIRE(probe_j_max(&b) == 37);
    b.usable = 1; REQUIRE(probe_j_max(&b) == 10);
    b.usable = 2; REQUIRE(probe_j_max(&b) == 40);
    b.usable = 0; REQUIRE(probe_j_max(&b) < 0);
    b.usable = 3; b.retry = 41; REQUIRE(probe_j_max(&b) < 0);
}
static void widths(void)
{
    const u32 clocks[] = {1, 5, 8};
    for (int mode = 0; mode < 2; mode++) for (int wide = 0; wide < 2; wide++)
    for (u32 c = 0; c < 3; c++) for (u32 phase = 0; phase < 8; phase++) {
        setup(mode, 0); in_us = 1; clock_cost_us = clocks[c]; out_us = 1; wide_h = wide;
        struct SplashRaster *s = pc98_splash_raster_state();
        probe_costs(s);
        probe_eight_us = (unsigned long long)8 * total * 100000 / den;
        scan_us += phase;
        test_max_closed = 0; test_min_open = ~0ULL;
        if0_max_in_run = 0;
        require_clockless_in = 1;
        REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
        require_clockless_in = 0;
        REQUIRE(if0_max_in_run > 2);
        probe_bounds(s);
        long long reference = wide ? (mode ? 80000 : 75472) : (mode ? 20000 : 24532);
        REQUIRE(probe_budget.b.min > 0);
        REQUIRE(probe_budget.b.min <= reference && probe_budget.b.max >= reference);
        REQUIRE(probe_budget.b.min >= reference - 3 * (long long)den);
        REQUIRE(probe_budget.b.max <= reference + 3 * (long long)den);
        unsigned long long highs = 0, samples = 0;
        for (u32 i = 0; i < PROBE_HPERIODS; i++) {
            highs += probe_highsamples[i]; samples += probe_hsamples[i];
        }
        long long measured = highs * 100000 / samples;
        REQUIRE(probe_delta(measured, reference) <= (unsigned long long)den);
        REQUIRE(test_max_closed * den <= (PROBE_HPERIODS + 6) * 100000);
        REQUIRE(test_min_open * den >= 100000);
        REQUIRE(!probe_errors && irq_on);
    }
    /* Six high samples can hide a seventh sample-width at the endpoints.
     * Taking the maximum individual duty still needs that uncertainty. */
    for (u32 i = 0; i < PROBE_HPERIODS; i++) {
        probe_hsamples[i] = 32; probe_highsamples[i] = 6;
    }
    probe_bounds(pc98_splash_raster_state());
    REQUIRE(probe_budget.b.max >= 7 * 100000 / 32);
    in_us = clock_cost_us = out_us = 1;
}
static void calibrated_width_error(void)
{
    /* Exact third Ra266 log. The two duty endpoints are 6/32 and 7/32.
     * The raw endpoints are ~5.933/6.925us; retaining calibrated Q error
     * gives ~4.933/7.925us, rather than bootstrap ~4.307/8.550us. */
    setup(1, 1);
    probe_eight_us = 113678;
    probe_cost[PROBE_Q] = (struct SplashRasterProbeCost){
        .upper_num_us = 13, .upper_samples = 8, .upper_us = 2 };
    probe_cost[PROBE_C] = (struct SplashRasterProbeCost){
        .upper_num_us = 21, .upper_samples = 2, .upper_us = 11 };
    probe_cost[PROBE_W].upper_samples = 1;
    probe_hd.cal[0].q_after = 3173;
    probe_hd.cal[1].q_after = 3146;
    for (u32 i = 0; i < PROBE_HPERIODS; i++) {
        probe_hsamples[i] = 32;
        probe_highsamples[i] = i ? 7 : 6;
    }
    probe_bounds(pc98_splash_raster_state());
    REQUIRE(probe_budget.q.max == 5114);
    REQUIRE(probe_budget.h.min == 99572 && probe_budget.h.max == 99617);
    REQUIRE(probe_budget.b.min == 15523 && probe_budget.b.max == 24938);
    REQUIRE(6 * probe_budget.h.min / 32 == 18669);
    REQUIRE((7 * probe_budget.h.max + 31) / 32 == 21792);
    /* Changing only the calibrated interval moves both endpoints by the
     * same amount in opposite directions, independently of bootstrap Q. */
    probe_hd.cal[1].q_after = 3000;
    probe_bounds(pc98_splash_raster_state());
    REQUIRE(probe_budget.b.min == 15669 && probe_budget.b.max == 24792);
}
static void pl_batch_peaks(void)
{
    setup(1, 0); in_us = clock_cost_us = out_us = 1;
    struct SplashRaster *s = pc98_splash_raster_state();
    probe_costs(s);
    probe_pl_columns_init();
    struct probe_pl_column *c = &probe_pl_columns[0];
    /* Known distribution: two maxima, one smaller runner-up, 61 lows.
     * Repeat with all equal values: second must explicitly be absent.
     * Every phase/kind uses the real 64-batch path and its own count. */
    for (u32 fixture = 1; fixture <= 2; fixture++)
    for (u32 phase = 0; phase < 2; phase++) for (u32 k = 0; k < PROBE_PL_KINDS; k++) {
        peak_fixture = fixture; peak_index = 0;
        unsigned long long one = 0;
        struct SplashRasterTimeRange cost = probe_pl_measure(s, k, c,
            k == PROBE_PL_EMPTY ? probe_pl_empty : probe_pl_emit5, phase, &one);
        peak_fixture = 0;
        const struct probe_pl_peak *p = &c->peak[phase][k];
        REQUIRE(!probe_errors && irq_on && peak_index == 64);
        REQUIRE(p->count > 1 && p->batches == 64);
        REQUIRE(p->max == ((fixture == 1 ? 12 : 8) * (long long)den + p->count - 1) / p->count);
        REQUIRE(p->hits == (fixture == 1 ? 2u : 64u));
        REQUIRE(p->second_valid == (fixture == 1));
        if (fixture == 1) REQUIRE(p->second == (10 * (long long)den + p->count - 1) / p->count);
        if (k == PROBE_PL_Q) {
            REQUIRE(p->seed == one && one > (unsigned long long)p->max);
            REQUIRE(cost.max == (long long)one);
        } else REQUIRE(!p->seed && cost.max == p->max);
    }
    /* An early maximum followed by a newer one resets frequency; a later
     * intermediate value replaces the former maximum as runner-up. */
    struct probe_pl_peak p = {0};
    const long long values[] = {4, 4, 9, 4, 6, 9, 6, 5};
    for (u32 i = 0; i < sizeof(values) / sizeof(values[0]); i++) probe_pl_peak_add(&p, values[i]);
    REQUIRE(p.max == 9 && p.hits == 2 && p.second == 6 && p.second_valid && p.batches == 8);
    /* Formatting includes W's same-phase EMPTY lower subtraction, and
     * zero-valued runner-ups remain distinguishable from absence. */
    probe_pl_count = 1;
    probe_pl_result[0].budget.den = den;
    c->peak[0][PROBE_PL_W] = (struct probe_pl_peak){
        .max = 120, .second = 100, .hits = 2, .batches = 64, .second_valid = 1, .count = 8 };
    c->empty_min[0] = 30;
    c->peak[1][PROBE_PL_W] = (struct probe_pl_peak){
        .max = 80, .second = 40, .hits = 1, .batches = 64, .second_valid = 1, .count = 4 };
    c->empty_min[1] = 40;
    probe_errors = 1; serial_init(SYS_SERIAL_BAUD); probe_pl_dump();
    REQUIRE(probe_contains("SPLR pl_peak=0 phase=0 kind=2 den=3147 count=8 batches=64 max_num=90 max_count=2 second_num=70 second_valid=1 seed_num=0 subtract_num=30\n"));
    REQUIRE(probe_contains("SPLR pl_peak=0 phase=1 kind=2 den=3147 count=4 batches=64 max_num=40 max_count=1 second_num=0 second_valid=1 seed_num=0 subtract_num=40\n"));
    REQUIRE(probe_contains("SPLR pl_peak=0 phase=0 kind=0"));
    REQUIRE(probe_contains("SPLR pl_peak=0 phase=1 kind=4"));
    u32 lines = 0;
    for (u32 i = 0; i < probe_output_size; i++) lines += probe_output[i] == '\n';
    REQUIRE(lines == 15); /* four summaries, column, 2*5 batch rows */
}
static void pl_details(void)
{
    setup(1, 0); in_us = 1; out_us = 2; clock_cost_us = 3;
    struct SplashRaster *s = pc98_splash_raster_state();
    probe_costs(s);
    probe_pl_columns_init();
    for (u32 n = 0; n < 2; n++) for (u32 k = 0; k < PROBE_PL_PARTS; k++)
        probe_pl_detail[n][k] = (struct probe_pl_detail){0};
    struct probe_pl_column *c = &probe_pl_columns[0];
    unsigned long long one = 0;
    for (u32 kind = 0; kind < PROBE_PL_SLOTS; kind++) {
        if (kind != PROBE_PL_Q && kind != PROBE_PL_PREP && kind != PROBE_PL_TRANS) continue;
        (void)probe_pl_measure(s, kind, c, probe_pl_emit5, 0, &one);
    }
    const struct probe_pl_detail *d = probe_pl_detail[0];
    REQUIRE(d[0].raw.batches == 64 && d[1].raw.batches == 1);
    REQUIRE(d[2].raw.batches == 64 && d[3].raw.batches == 64);
    REQUIRE(d[1].raw.max == 4 * (long long)den);
    REQUIRE(d[1].upper.max == 2 * (long long)den);
    REQUIRE(d[1].correction.max == -2 * (long long)den);
    for (u32 k = 0; k < PROBE_PL_PARTS; k++) {
        REQUIRE(d[k].raw.hits == d[k].raw.batches);
        REQUIRE(d[k].upper.max == d[k].raw.max + d[k].correction.max);
    }
    pl_script = 1; pl_reads = 0;
    success_entry_cost = 2; success_return_cost = 5;
    probe_pl_edge_upper = 64;
    unsigned int saved = irq_save();
    u32 before_writes = writes;
    /* Successful return + prepare's OUT + next wait entry + one clock.
     * No poll or successful-body cost belongs to this linkage interval. */
    REQUIRE(probe_pl_batch(PROBE_PL_TRANS, c, probe_pl_emit5, 1, 0) == 12);
    REQUIRE(if0_in_reads == 6 && pl_reads == 6 && writes == before_writes + 1);
    REQUIRE(probe_pl_batch_polls == 4);
    REQUIRE(probe_pl_batch_total == 3 * 3 + 2 * (2 + 5) + 6 * in_us + out_us);
    irq_restore(saved);
    const probe_pl_emit_fn emitters[] = { probe_pl_empty, probe_pl_emit1,
        probe_pl_emit2, probe_pl_emit3, probe_pl_emit4, probe_pl_emit5, probe_pl_emit6 };
    /* Exact independent body: high/low/low/high INs + N OUT + one clock.
     * Independently timed outside cost: entry + return + two clocks.
     * Vary I/O and linkage costs to ensure the two intervals stay disjoint. */
    for (u32 n = 0; n <= 6; n++) for (u32 phase = 0; phase < 2; phase++) {
        struct probe_pl_column *j = &probe_pl_joint[n];
        *j = *c; j->n = n;
        j->row[0] = probe_pl_columns[42].row[0];
        j->row[1] = probe_pl_columns[42].row[1];
        j->row[0].count = j->row[1].count = n;
        for (u32 cost = 1; cost <= 2; cost++) {
            in_us = cost; out_us = cost + 1;
            before_writes = writes; pl_reads = 0;
            success_entry_cost = cost * 2; success_return_cost = cost * 5;
            unsigned int flags = irq_save();
            require_clockless_in = 1;
            unsigned long long span = probe_pl_batch(PROBE_PL_JOINT, j, emitters[n], 1, phase);
            require_clockless_in = 0;
            REQUIRE(span == 3 + 4 * cost + n * (cost + 1));
            REQUIRE(probe_pl_batch_outside == 6 + 7 * cost);
            REQUIRE(probe_pl_batch_total == span + probe_pl_batch_outside);
            REQUIRE(probe_pl_batch_polls == 4);
            REQUIRE(if0_in_reads == 4 && if0_end_clock == 2);
            REQUIRE(writes - before_writes == n);
            irq_restore(flags);
        }
        in_us = out_us = 1;
        success_entry_cost = success_return_cost = 0;
        peak_fixture = 1; peak_index = 0;
        (void)probe_pl_measure(s, PROBE_PL_JOINT, j, emitters[n], phase, &one);
        peak_fixture = 0;
        REQUIRE(j->peak[phase][PROBE_PL_JOINT].hits == 2);
        REQUIRE(j->peak[phase][PROBE_PL_JOINT].second_valid);
        const struct probe_pl_detail *outside = &probe_pl_outside[n][phase];
        REQUIRE(outside->raw.batches == 64 && outside->raw.max == 6 * (long long)den);
        REQUIRE(outside->upper.max == 2 * (long long)den);
        REQUIRE(outside->correction.max == -4 * (long long)den);
        REQUIRE(probe_pl_joint_polls[n][phase].batches == 64);
    }
    pl_script = 0;
    struct probe_pl_stamp stamp = { .clocks = PROBE_PL_STAMP_BEGIN | PROBE_PL_STAMP_END };
    u32 iterations = 0;
    before_writes = writes;
    REQUIRE(probe_pl_success(&c->row[0], probe_pl_emit5, 0, 0, 1, 1, 1, 0, &iterations, &stamp) == -1);
    REQUIRE(iterations == 1 && writes == before_writes);
    REQUIRE(probe_pl_success(&c->row[0], probe_pl_emit5, 0, 0, 2, 2, 1, 0, &iterations, &stamp) == -1);
    REQUIRE(iterations == 1 && writes == before_writes);
    REQUIRE(probe_pl_success(&c->row[0], probe_pl_emit5, 0, 0, 1, 2, 1, 1, &iterations, &stamp) == -1);
    REQUIRE(iterations == 1 && writes == before_writes);
    REQUIRE(probe_pl_success(&c->row[0], probe_pl_emit5, 0, 0, 1, 2, 0, 0, &iterations, &stamp) == -1);
    REQUIRE(iterations == 2 && writes == before_writes);
    probe_errors = 1; serial_init(SYS_SERIAL_BAUD);
    probe_pl_result[0].budget.den = den;
    probe_pl_details_dump();
    REQUIRE(probe_contains("SPLR pl_q_part=1 n=5 den=3147 raw_max_num=12588 raw_max_count=1"));
    REQUIRE(probe_contains("clock_delta_max_num=-6294 clock_delta_max_count=1"));
    REQUIRE(probe_contains("SPLR pl_joint_n=0 phase=0 scan_reason=0 den=3147"));
    REQUIRE(probe_contains("SPLR pl_joint_n=6 phase=1 scan_reason=0 den=3147"));
    REQUIRE(probe_contains("SPLR pl_joint_outer_n=6 phase=1 scan_reason=0 den=3147 raw_max_num=18882"));
    REQUIRE(probe_contains("polls_max_num=4 polls_max_count=64"));
    probe_pl_result[0].prep = 10; probe_pl_result[0].transition = 20;
    probe_pl_detail[0][3].upper.max = 24;
    probe_pl_details_dump();
    REQUIRE(probe_contains(" link_budget_num=30 link_upper_num=24 link_gap_num=6"));
    REQUIRE(probe_contains(" max_count=2 second_num="));
    /* Preserve period order and total counts, including a partial last row. */
    probe_hd.completed = 5;
    for (u32 i = 0; i < 5; i++) {
        probe_highsamples[i] = i == 2 ? 8 : i == 3 ? 6 : 7;
        probe_hsamples[i] = 31 + i;
    }
    probe_hsamples_dump();
    REQUIRE(probe_contains("SPLR b_samples=0 7:31 7:32 8:33 6:34\n"));
    REQUIRE(probe_contains("SPLR b_samples=4 7:35\n"));
    REQUIRE(probe_contains("SPLR b_high_samples max_num=8 max_count=1 second_num=7 second_valid=1 samples=5\n"));
    in_us = out_us = clock_cost_us = 1;
}
static void raw_measurements(void)
{
    struct probe_raw_bit varied = {0};
    probe_raw_run(&varied, 0, 3); probe_raw_run(&varied, 0, 7); probe_raw_run(&varied, 0, 2);
    REQUIRE(varied.min[0] == 2 && varied.max[0] == 7);
    setup(1, 0); in_us = 2; clock_cost_us = 3;
    for (u32 p = 0; p < 2; p++) {
        raw_reads = raw_clocks = 0;
        raw_port = p ? GDC_TEXT_STAT : GDC_GFX_STAT;
        raw_test = 1;
        if (!p) { irq_at[0] = scan_us + 100; irq_delay[0] = 17; }
        probe_raw_scan(raw_port, &probe_raw[p]);
        raw_test = 0;
        const struct probe_raw *r = &probe_raw[p];
        REQUIRE(raw_reads == 32768 && raw_clocks == 16);
        REQUIRE(r->both == 0 && r->either == 255);
        REQUIRE(r->elapsed == 65560 + (p ? 0 : 17));
        for (u32 v = 0; v < 256; v++) REQUIRE(r->freq[v] == 128);
        for (u32 k = 0; k < 2; k++) {
            const struct probe_raw_bit *b = &r->bit[k];
            u32 edges = k ? 256 : 512, len = k ? 64 : 32;
            REQUIRE(b->high == 16384 && b->low == 16384);
            REQUIRE(b->rise == edges - (p ? 8 : 0));
            REQUIRE(b->fall == edges - (p ? 0 : 8));
            REQUIRE(b->min[0] == len && b->max[0] == len);
            REQUIRE(b->min[1] == len && b->max[1] == len);
        }
    }
    probe_errors = 1; serial_init(SYS_SERIAL_BAUD);
    probe_measure_dump();
    REQUIRE(probe_contains("SPLR h_reason=0 count=0 bursts=0 completed=0 rise=0 fall=0 high=0 low=0 min_us=0 max_us=0 if0_over=0\n"));
    REQUIRE(probe_contains("SPLR raw_port=160 blocks=8 reads=4096 and=0 or=255 elapsed_us=65577\n"));
    REQUIRE(probe_contains("SPLR raw_port=96 blocks=8 reads=4096 and=0 or=255 elapsed_us=65560\n"));
    REQUIRE(probe_contains("SPLR raw_port=160 bit=5 high=16384 low=16384 rise=512 fall=504 low_min=32 low_max=32 high_min=32 high_max=32\n"));
    REQUIRE(probe_contains("SPLR raw_port=96 bit=6 high=16384 low=16384 rise=248 fall=256 low_min=64 low_max=64 high_min=64 high_max=64\n"));
    REQUIRE(probe_contains("SPLR raw_hist=160 value:count 0:128 1:128"));
    REQUIRE(probe_contains(" 254:128 255:128\n"));
    u32 lines = 0;
    for (u32 i = 0; i < probe_output_size; i++) lines += probe_output[i] == '\n';
    REQUIRE(lines == 42);
    /* Fixed H and absent values: only nonzero histogram bins are printed. */
    setup(1, 0); missing_h = 1; in_us = clock_cost_us = 1;
    probe_raw_scan(GDC_GFX_STAT, &probe_raw[0]);
    probe_raw_scan(GDC_TEXT_STAT, &probe_raw[1]);
    REQUIRE(probe_raw[0].bit[1].low == 32768 && !probe_raw[0].bit[1].high);
    REQUIRE(probe_raw[0].bit[1].min[0] == 4096 && probe_raw[0].bit[1].max[0] == 4096);
    REQUIRE(!probe_raw[0].bit[1].min[1] && !probe_raw[0].bit[1].max[1]);
    probe_errors = 1; serial_init(SYS_SERIAL_BAUD); probe_measure_dump();
    REQUIRE(!probe_contains(" 64:") && !probe_contains(" 255:") && !probe_contains(":0"));
}
static void h_limit_inputs(void)
{
    /* Exact second Ra266 log, review counterexample, and the rounded ratio
     * boundary: 3147/2793 is admitted, 3147/2792 exceeds the cap. */
    const struct { u32 q, lower, count, cap, admitted; } cases[] = {
        {3147, 3120, 1060, 1184, 1},
        {3163, 3105, 1065, 1178, 1},
        {3147, 2793, 1184, 1184, 1},
        {3147, 2792, 1185, 1184, 0},
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        setup(1, 0); in_us = clock_cost_us = out_us = 1;
        struct SplashRaster *s = pc98_splash_raster_state();
        probe_costs(s);
        probe_eight_us = (unsigned long long)8 * total * 100000 / den;
        probe_cost[PROBE_C].upper_us = 11; /* C=(2*11+1)*3147=72381 */
        fixture_q = cases[i].q; fixture_lower = cases[i].lower;
        u32 completed = probe_hblank(s);
        fixture_q = fixture_lower = 0;
        REQUIRE(probe_hd.den == 3147 && probe_hd.clocks == 72381);
        REQUIRE(probe_hd.limit == 3800000);
        REQUIRE(probe_hd.count == cases[i].count && probe_hd.cap == cases[i].cap);
        REQUIRE(probe_hd.excess == (long long)cases[i].count * cases[i].q + 72381 - 3800000);
        if (cases[i].admitted) {
            REQUIRE(completed == PROBE_HPERIODS && probe_hd.bursts == 1 && probe_hd.reason == 1);
            REQUIRE(!probe_errors && !probe_hd.over);
        } else {
            REQUIRE(!completed && !probe_hd.bursts && probe_hd.reason == 3);
            REQUIRE(probe_hd.budget_stage == 5 && probe_errors == 1);
        }
        REQUIRE(irq_on);
    }
}
static void h_diagnostics(void)
{
    setup(1, 0); in_us = clock_cost_us = out_us = 1;
    struct SplashRaster *s = pc98_splash_raster_state();
    probe_costs(s); probe_eight_us = (unsigned long long)8 * total * 100000 / den;
    h_test = 1;
    REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
    h_test = 0;
    REQUIRE(probe_hd.reason == 1 && probe_hd.completed == PROBE_HPERIODS);
    REQUIRE(probe_hd.high == h_high && probe_hd.low == h_reads - h_high);
    REQUIRE(probe_hd.rise == h_rise && probe_hd.fall == h_fall);
    REQUIRE(probe_hd.high + probe_hd.low == probe_hd.count * probe_hd.bursts);
    REQUIRE(probe_hd.min_us == probe_hd.count + clock_cost_us);
    REQUIRE(probe_hd.max_us == probe_hd.min_us && !probe_hd.over);
    /* Same calibration, slower actual loop: preserve the completed count
     * and raw edges even when the first IF0 burst overruns. */
    slow_capture = 1;
    REQUIRE(!probe_hblank(s));
    REQUIRE(probe_hd.reason == 8 && probe_hd.over == 1 && probe_hd.bursts == 1);
    REQUIRE(probe_hd.completed > 0 && probe_hd.max_us > 60 && irq_on);
    /* The former real-log count=18 no longer loses armed at each burst. */
    slow_capture = 0; probe_error_reset();
    probe_cost[PROBE_Q].upper_num_us = 37;
    probe_cost[PROBE_Q].upper_samples = 16;
    probe_cost[PROBE_C].upper_num_us = 10;
    probe_cost[PROBE_C].upper_samples = 1;
    probe_cost[PROBE_C].upper_us = 100;
    /* Large uncertainty must not buy a longer IF=0 region. */
    REQUIRE(!probe_hblank(s));
    REQUIRE(probe_hd.reason == 3 && probe_hd.budget_stage == 5 && !probe_hd.bursts && irq_on);
    probe_error_reset();
    probe_cost[PROBE_C].upper_samples = 8;
    probe_cost[PROBE_C].upper_us = 2;
    REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
    REQUIRE(probe_hd.reason == 1 && probe_hd.count > 1000 && probe_hd.completed == PROBE_HPERIODS);
    REQUIRE(probe_hd.rise && probe_hd.fall && probe_hd.high && probe_hd.low);
    REQUIRE(probe_hd.min_us == probe_hd.count + 1 && probe_hd.max_us == probe_hd.min_us && !probe_hd.over);
    setup(1, 0); in_us = clock_cost_us = out_us = 1;
    s = pc98_splash_raster_state();
    probe_costs(s); missing_h = 1;
    REQUIRE(!probe_hblank(s));
    REQUIRE(probe_hd.reason == 6 && !probe_hd.completed && !probe_hd.rise && !probe_hd.fall);
    REQUIRE(!probe_hd.high && probe_hd.low == probe_hd.bursts * probe_hd.count);
    REQUIRE(probe_hd.bursts == 1 && !probe_hd.over);
    probe_cost[PROBE_Q].upper_num_us = 0;
    REQUIRE(!probe_hblank(s) && probe_hd.reason == 2 && !probe_hd.bursts);
    probe_cost[PROBE_Q].upper_num_us = 73;
    probe_cost[PROBE_Q].upper_samples = 64;
    missing_h = 0; probe_error_reset();
    probe_q_lower = 1;
    REQUIRE(probe_hblank(s) == PROBE_HPERIODS); /* refine the coarse lower bound */
    probe_cost[PROBE_C].upper_num_us = 100000;
    probe_cost[PROBE_C].upper_us = 100000;
    REQUIRE(!probe_hblank(s) && probe_hd.reason == 3);
    REQUIRE(probe_hd.budget_stage == 1);
    setup(1, 0); s = pc98_splash_raster_state(); probe_costs(s);
    frozen = 1; probe_open_until = scan_us + 100;
    REQUIRE(!probe_hblank(s) && probe_hd.reason == 5 && !probe_hd.bursts);
}
static void pl_normal_windows(void)
{
    /* Exact IN/OUT costs from the independent clock, with no timestamp in
     * the scan. Exercise a certified window rather than the fallback path. */
    for (int mode = 0; mode < 2; mode++) for (u32 phase = 0; phase < 42; phase++) {
        setup(mode, 0); in_us = clock_cost_us = out_us = 1;
        struct SplashRaster *s = pc98_splash_raster_state();
        probe_pl_columns_init();
        scan_us = phase;
        probe_open_until = 0;
        for (u32 i = 0; i < probe_pl_count; i++) {
            struct probe_pl_column *c = &probe_pl_columns[i];
            struct probe_pl_result r = { .budget = { .den = den,
                .h = {100000, 100000}, .q = {den, 2 * den},
                .w = {c->n * den, c->n * den} },
                .poll = {9 * den / 10, den}, .prep = den };
            probe_pl_window(&r);
            REQUIRE(r.window_ok && r.lower > 1);
            require_clockless_in = 1;
            probe_pl_scan(s, c, &r);
            require_clockless_in = 0;
            REQUIRE(c->completed == PROBE_HPERIODS && !c->early && !c->timeout);
            REQUIRE(c->window_ok && irq_on);
        }
    }
}
static void realhw_precision(void)
{
    /* Ra266 input: 31k VS, PIT=24576, 1us clock quantum, IN ~1.15us.
     * Fixed RAM-loop entry cost is a synthetic 1us: the old log did not
     * preserve one/span/Q after calibration, so it cannot supply them. */
    setup(1, 1); in_us = out_us = 1; in_milli = 150;
    clock_cost_us = 9; poll_entry_us = 1;
    struct SplashRaster *s = pc98_splash_raster_state();
    probe_costs(s);
    probe_cost[PROBE_Q].upper_num_us = 13;
    probe_cost[PROBE_Q].upper_samples = 8;
    probe_cost[PROBE_C].upper_us = 11;
    REQUIRE(probe_pit.reload == 24576 && s->mode == SPLASH_RASTER_31K);
    probe_eight_us = (unsigned long long)8 * total * 100000 / den;
    require_clockless_in = 1;
    REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
    require_clockless_in = 0;
    const struct probe_hcal *first = &probe_hd.cal[0], *last = &probe_hd.cal[1];
    REQUIRE(first->count == 298 && probe_hd.clocks == 23 * den);
    u32 old_cap = (34 * 100000 - probe_hd.clocks) / first->upper;
    u32 old_count = (33 * 100000 + first->upper + first->lower - 1) / first->lower + 1;
    REQUIRE(old_count > old_cap); /* old path rejects */
    REQUIRE(last->count > first->count && last->q_before == first->q_after);
    REQUIRE(last->lower_before == first->lower && last->q_after == last->upper);
    REQUIRE(last->lower <= 1150 * den / 1000 && last->upper >= (1150 * den + 999) / 1000);
    REQUIRE(probe_hd.count <= probe_hd.cap && probe_hd.excess <= 0);
    REQUIRE(probe_hd.reason == 1 && !probe_hd.budget_stage && probe_hd.cal_pass == 2);
    REQUIRE(last->span_min > last->one + 2 && last->span_min <= last->span_max &&
            last->one && irq_on && !probe_errors);
    REQUIRE(test_max_closed * den <= 38 * 100000 && test_min_open * den >= 100000);
    /* Check the new post-IF diagnostic fields against the retained RAM. */
    probe_errors = 1; serial_init(SYS_SERIAL_BAUD); probe_measure_dump();
    REQUIRE(probe_contains("SPLR h_budget_stage=0 cal_pass=2 den=3147 clocks_num=72381 limit_num=3800000"));
    REQUIRE(probe_contains("SPLR h_cal=1 den=3147 q_before_num=5114"));
    REQUIRE(probe_contains("SPLR h_cal=2 den=3147 q_before_num="));
    REQUIRE(probe_contains(" lower_before_num=") && probe_contains(" span_min_us=") &&
            probe_contains(" span_max_us=") && probe_contains(" upper_num=") &&
            probe_contains(" excess_num=-"));
    /* Print these three lines as the next real-hardware script's example. */
    u32 begin = 0;
    while (begin < probe_output_size && probe_output[begin++] != '\n') { }
    u32 end = begin, lines = 0;
    while (end < probe_output_size && lines < 3) lines += probe_output[end++] == '\n';
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(probe_output + begin), "d"(end - begin) : "memory");
    /* All twenty phases of a .15us fractional input against the 1us
     * clock must reach capture, including a rounded-up short sandwich. */
    for (u32 phase = 0; phase < 20; phase++) {
        setup(1, 1); in_us = out_us = 1; in_milli = 150;
        clock_cost_us = 9; poll_entry_us = 1;
        s = pc98_splash_raster_state();
        probe_costs(s);
        probe_cost[PROBE_Q].upper_num_us = 13;
        probe_cost[PROBE_Q].upper_samples = 8;
        probe_cost[PROBE_C].upper_us = 11;
        scan_milli = phase * 50;
        require_clockless_in = 1;
        REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
        require_clockless_in = 0;
        /* Repeated one-poll sandwiches retain 9us clock + 1us entry +
         * 1us IN, even when the first IN rounds up to 2us. */
        REQUIRE(probe_hd.cal[0].one == 11 && probe_hd.cal[1].one == 11);
        REQUIRE(probe_hd.count <= probe_hd.cap && probe_hd.excess <= 0);
        REQUIRE(!probe_errors && irq_on && test_max_closed * den <= 3800000);
    }
    /* The recorded count constrains Q lower, not the IF=1 raw IN average.
     * A synthetic .991us RAM loop retains the bootstrap Q and C. */
    setup(1, 1); in_us = 0; in_milli = 991;
    clock_cost_us = 9; poll_entry_us = 1;
    s = pc98_splash_raster_state();
    scan_milli = 0;
    probe_c_min = 9; probe_q_lower = 1; probe_open_until = 0;
    probe_cost[PROBE_Q] = (struct SplashRasterProbeCost){ .total_us = 281, .samples = 256,
        .upper_num_us = 13, .upper_samples = 8 };
    probe_cost[PROBE_C] = (struct SplashRasterProbeCost){ .upper_us = 11 };
    require_clockless_in = 1;
    REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
    require_clockless_in = 0;
    first = &probe_hd.cal[0];
    old_count = (33 * 100000 + first->upper + first->lower - 1) / first->lower + 1;
    old_cap = (34 * 100000 - probe_hd.clocks) / first->upper;
    REQUIRE(first->count == 298 && old_count > old_cap);
    REQUIRE(probe_hd.count <= probe_hd.cap && !probe_errors && irq_on);
}
static void np_fast_poll(void)
{
    /* NP21/W log: 24k, 8us clock, IN ~0.6us. The former <=8-poll
     * sandwich has no positive differential after two 1us quanta. */
    setup(0, 0); wide_h = 1; in_us = 0; in_milli = 600;
    clock_cost_us = 8; out_us = 1;
    struct SplashRaster *s = pc98_splash_raster_state();
    probe_costs(s);
    probe_eight_us = (unsigned long long)8 * total * 100000 / den;
    REQUIRE(probe_hblank(s) == PROBE_HPERIODS);
    probe_pl_columns_init();
    unsigned long long one = probe_pl_batch(PROBE_PL_Q, &probe_pl_columns[0], probe_pl_emit5, 1, 0);
    u32 old_count = 8;
    while (old_count > 1 && (old_count * (one + 1) +
            2 * probe_cost[PROBE_C].upper_us) * den > 200000) old_count /= 2;
    unsigned long long short_span = ~0ULL;
    for (u32 i = 0; i < PROBE_COST_BATCH; i++) {
        unsigned long long span = probe_pl_batch(PROBE_PL_Q, &probe_pl_columns[0], probe_pl_emit5, old_count, 0);
        if (span < short_span) short_span = span;
    }
    REQUIRE(short_span <= one + 2 * SPLASH_RASTER_CLOCK_QUANTUM_US);
    require_clockless_in = 1;
    probe_pl_measure_all(s);
    require_clockless_in = 0;
    REQUIRE(!probe_errors && irq_on);
    for (u32 i = 0; i < 129; i++) {
        const struct probe_pl_column *c = &probe_pl_columns[i];
        REQUIRE(c->poll.min >= den / 2 && c->completed == PROBE_HPERIODS && c->elapsed_us > 0);
        REQUIRE(c->scan_reason == 1 && !c->early && !c->timeout);
    }
    /* A genuine zero lower bound must still refuse division and say why. */
    setup(0, 0); in_us = 0; clock_cost_us = out_us = 1;
    probe_costs(pc98_splash_raster_state());
    probe_pl_measure_all(pc98_splash_raster_state());
    REQUIRE(!probe_errors && !probe_pl_result[0].qualified && !probe_pl_result[1].qualified);
    for (u32 i = 0; i < 129; i++) {
        REQUIRE(probe_pl_columns[i].scan_reason == 2);
        REQUIRE(!probe_pl_columns[i].elapsed_us && !probe_pl_columns[i].completed);
    }
    probe_errors = 1; serial_init(SYS_SERIAL_BAUD); probe_pl_dump();
    REQUIRE(probe_contains(" elapsed_us=0 scan_reason=2\n"));
    in_us = clock_cost_us = out_us = 1;
}
static void pl_phase_errors(void)
{
    for (u32 fixture = PL_ERROR_PHASE1; fixture <= PL_ERROR_JOINT_PHASE1; fixture++) {
        setup(1, 0); in_us = clock_cost_us = out_us = 1;
        pl_error_fixture = fixture;
        platform_splash_probe();
        REQUIRE(irq_on && probe_errors == 1 && !probe_fatal);
        REQUIRE(probe_entry_count == PROBE_ENTRIES && probe_frames[PROBE_FRAMES-1].duration);
        if (fixture == PL_ERROR_PHASE1) {
            REQUIRE(probe_error_first.context.col == 4 && probe_error_first.context.phase == 1);
            REQUIRE(probe_pl_columns[4].scan_reason == PROBE_SCAN_ERROR);
            const struct probe_pl_result *r = &probe_pl_result[0];
            long long w = 0, d = 0, prep = 0, transition = 0;
            for (u32 i = 0; i < probe_pl_count; i++) {
                const struct probe_pl_column *c = &probe_pl_columns[i];
                if (c->n != 5 || i == 4) continue;
                if (c->w > w) w = c->w;
                if (c->d > d) d = c->d;
                if (c->prep > prep) prep = c->prep;
                for (u32 phase = 0; phase < 2; phase++)
                    if ((long long)(2 * c->peak[phase][PROBE_PL_Q].seed) > transition)
                        transition = 2 * c->peak[phase][PROBE_PL_Q].seed;
            }
            REQUIRE(probe_pl_columns[4].w > w && probe_pl_columns[4].d > d &&
                    probe_pl_columns[4].prep > prep);
            REQUIRE(r->budget.w.max == w && r->budget.d.max == d && r->prep == prep);
            REQUIRE(r->transition == transition);
            REQUIRE(probe_pl_columns[128].completed == PROBE_HPERIODS);
        } else {
            u32 phase = fixture - PL_ERROR_JOINT_PHASE0;
            REQUIRE(probe_error_first.context.col == PROBE_CONTEXT_NONE);
            REQUIRE(probe_error_first.context.n == 2 && probe_error_first.context.phase == phase);
            REQUIRE(probe_pl_joint[2].peak[phase][PROBE_PL_JOINT].batches == 3);
            REQUIRE(probe_pl_outside[2][phase].raw.batches == 3);
            REQUIRE(probe_contains("SPLR pl_joint_n=1 phase=1 scan_reason=1"));
            if (phase) REQUIRE(probe_contains("SPLR pl_joint_n=2 phase=0 scan_reason=1"));
            REQUIRE(probe_contains(phase == 0 ? "SPLR pl_joint_n=2 phase=0 scan_reason=5" :
                                               "SPLR pl_joint_n=2 phase=1 scan_reason=5"));
            REQUIRE(probe_contains(phase == 0 ? "SPLR pl_joint_outer_n=2 phase=0 scan_reason=5" :
                                               "SPLR pl_joint_outer_n=2 phase=1 scan_reason=5"));
            REQUIRE(probe_contains("SPLR pl_joint_n=3 phase=0 scan_reason=6"));
            REQUIRE(probe_contains("SPLR pl_joint_outer_n=6 phase=1 scan_reason=6"));
            if (!phase) REQUIRE(probe_contains("SPLR pl_joint_n=2 phase=1 scan_reason=6"));
            REQUIRE(probe_pl_joint[6].peak[1][PROBE_PL_JOINT].batches == PROBE_COST_BATCH);
        }
    }
}
static void pl_error_reasons(void)
{
    for (u32 fixture = 1; fixture <= 2; fixture++) {
        setup(1, 0); in_us = clock_cost_us = out_us = 1;
        pl_error_fixture = fixture;
        platform_splash_probe();
        REQUIRE(irq_on && probe_errors == 1 && !probe_fatal);
        u32 reason = fixture == 1 ? PROBE_ERR_PL_ALIGN : PROBE_ERR_PL_SUCCESS;
        REQUIRE(probe_error_first.reason == reason && probe_error_counts[reason] == 1);
        REQUIRE(probe_error_first.context.col == 4 && probe_error_first.context.phase == 0);
        REQUIRE(probe_error_first.context.n == 5 && probe_error_first.context.kind == PROBE_PL_TRANS);
        REQUIRE(probe_error_first.value > 0);
        REQUIRE(probe_contains(fixture == 1 ? "SPLR error_first reason=14 col=4 phase=0 n=5 kind=5 value=" :
                                             "SPLR error_first reason=1 col=4 phase=0 n=5 kind=5 value="));
        REQUIRE(probe_contains(fixture == 1 ? " reason_14=1" : " reason_1=1"));
        REQUIRE(probe_pl_columns[4].scan_reason == PROBE_SCAN_ERROR);
        REQUIRE(probe_pl_columns[5].scan_reason == PROBE_SCAN_AFTER_ERROR);
        REQUIRE(probe_pl_columns[128].scan_reason == PROBE_SCAN_AFTER_ERROR);
        REQUIRE(probe_pl_columns[128].completed == PROBE_HPERIODS);
        REQUIRE(probe_pl_joint[6].peak[1][PROBE_PL_JOINT].batches == PROBE_COST_BATCH);
        REQUIRE(!probe_pl_result[0].qualified && !probe_pl_result[1].qualified);
        REQUIRE(probe_entry_count == PROBE_ENTRIES && probe_frames[PROBE_FRAMES-1].duration);
    }
    setup(1, 0); backwards = 1;
    platform_splash_probe();
    REQUIRE(probe_fatal && probe_error_counts[PROBE_ERR_CLOCK_BACKWARD]);
    REQUIRE(!probe_pl_count && !probe_entry_count && !probe_frames[0].duration);
    REQUIRE(probe_contains("SPLR error_first reason=3"));
}
void _start(void)
{
    /* L1: a 5us clock must not turn the 31k 6.36us pulse into B_min=0. */
    setup(1, 0); in_us = 1; clock_cost_us = 5; out_us = 1;
    platform_splash_probe();
    for (u32 p = 0; p < 2; p++) {
        u32 samples = 0;
        for (u32 v = 0; v < 256; v++) samples += probe_raw[p].freq[v];
        REQUIRE(samples == 32768);
    }
    REQUIRE(probe_budget.b.min > 0);
    REQUIRE(probe_budget.b.min <= 20000 && probe_budget.b.max >= 20000);
    REQUIRE(probe_budget.b.min >= 20000 - 2 * (long long)den);
    REQUIRE(probe_budget.b.max <= 20000 + 2 * (long long)den);
    REQUIRE(probe_budget.q.max + probe_budget.w.max + probe_budget.error < 20000);
    /* Preserve the actual B bounds and cost fractions in the test log. */
    for (u32 i = 0; i < probe_output_size; i++) {
        const char *prefix = "SPLR budget_den=";
        u32 j = 0;
        while (prefix[j] && prefix[j] == probe_output[i+j]) j++;
        if (!prefix[j]) {
            u32 n = 0;
            while (i+n < probe_output_size && probe_output[i+n++] != '\n') { }
            __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(probe_output+i), "d"(n) : "memory");
            break;
        }
    }
    in_us = clock_cost_us = out_us = 1;
    pl_phase_errors(); pl_error_reasons();
    costs(); budgets(); pl_normal_windows(); widths(); raw_measurements(); h_limit_inputs(); h_diagnostics();
    realhw_precision(); np_fast_poll(); calibrated_width_error(); pl_batch_peaks(); pl_details();
    for (int mode = 0; mode < 2; mode++) for (int vs = 0; vs < 2; vs++) for (int wide = 0; wide < 2; wide++) {
        setup(mode, vs);
        wide_h = wide;
        platform_splash_probe_before();
        REQUIRE(probe_bios_reads == 1);
        REQUIRE(probe_bios_before == probe_bios_snapshot[PEGC_BIOS_PRXCRT]);
        platform_splash_probe();
        REQUIRE(probe_bios_reads == 2);
        REQUIRE(probe_contains(mode ? " bios_before=32 bios_after=32" :
                                     " bios_before=0 bios_after=0"));
        REQUIRE(!probe_errors && probe_rx_count == 8 && !probe_rx_missing && !probe_rx_bytes);
        REQUIRE(probe_presents == 1 && probe_rects > 1000);
        REQUIRE(probe_frames[PROBE_FRAMES-1].duration > 0);
        REQUIRE(probe_frames[PROBE_FRAMES-1].closed_total > 0);
        REQUIRE(probe_wave == (u32)(1 + mode * 2 + vs) && probe_period_ok);
        REQUIRE(probe_eight_us > 8 * 14000 && probe_high_us > 0);
        REQUIRE(probe_f_lo <= probe_f_hi && probe_h_lo <= probe_h_hi);
        REQUIRE(probe_f_lo > 0 && probe_h_lo > probe_f_lo);
        unsigned long long frame_units = (unsigned long long)total * 100000;
        unsigned long long actual_f = probe_f_lo * den / frame_units * frame_units + high_lines * 100000;
        unsigned long long actual_h = actual_f + 100000 -
            (wide ? (mode ? 80000 : 75472) : (mode ? 20000 : 24532));
        REQUIRE(probe_f_lo * den <= actual_f && actual_f <= probe_f_hi * den);
        REQUIRE(probe_h_lo * den <= actual_h && actual_h <= probe_h_hi * den);
        REQUIRE(probe_entry_count == PROBE_ENTRIES);
        REQUIRE(probe_entry[PROBE_ENTRIES-1].line == 396);
        REQUIRE(probe_entry[0].initial_margin > 0);
        for (u32 i = 0; i < probe_entry_count; i++) {
            REQUIRE(!probe_entry[i].qualified);
            REQUIRE(probe_entry[i].budget.j_max == probe_j_max(&probe_entry[i].budget));
            REQUIRE(probe_entry[i].frame_j_max <= probe_entry[i].capture_margin);
            REQUIRE(probe_entry[i].frame_j_max <= probe_entry[i].initial_margin);
        }
        REQUIRE(probe_contains("sample=clock_read_us n=64"));
        REQUIRE(probe_contains("sample=vsync_us n=8"));
        REQUIRE(probe_contains("sample=hperiod_us n=32"));
        REQUIRE(probe_contains(" begin_us=") && probe_contains(" end_us="));
        REQUIRE(probe_contains("errors=0") && probe_contains("rx_words=8"));
        REQUIRE(probe_contains("commit=host-probe crc=123"));
        REQUIRE(probe_contains("SPLR cost=0 num_us=512 den=512 upper_us=2 upper_num_us=73 upper_den=64"));
        REQUIRE(probe_contains("SPLR entry_y=396"));
        REQUIRE(probe_pl_count == 129);
        for (u32 n = 0; n < 2; n++) {
            REQUIRE(probe_pl_detail[n][1].raw.batches == (n ? 174u : 84u));
            REQUIRE(probe_pl_detail[n][0].upper.batches == (n ? 174u : 84u) * 64);
            REQUIRE(probe_pl_detail[n][3].upper.batches == (n ? 174u : 84u) * 64);
            REQUIRE(probe_pl_result[n].budget.q.max == probe_pl_result[n].poll.max +
                    probe_pl_result[n].prep + probe_pl_result[n].transition);
        }
        for (u32 n = 0; n <= 6; n++) for (u32 phase = 0; phase < 2; phase++) {
            REQUIRE(probe_pl_joint[n].peak[phase][PROBE_PL_JOINT].batches == 64);
            const struct PlRow *row = &probe_pl_joint[n].row[phase];
            REQUIRE(row->count == n);
            if (n) REQUIRE(row->port[n-1] != PAL_IDX_PORT);
            if (n == 3) {
                REQUIRE(row->port[0] == PAL_G_PORT && row->port[1] == PAL_R_PORT);
                REQUIRE(row->port[2] == PAL_B_PORT && row->value[2] == (phase ? 0 : 15));
            }
        }
        REQUIRE(probe_contains("SPLR b_samples=28 "));
        REQUIRE(probe_contains("SPLR pl_q_contract=5"));
        REQUIRE(probe_contains("SPLR pl_q_part=3 n=6"));
        REQUIRE(probe_pl_result[0].columns == 42 && probe_pl_result[1].columns == 87);
        REQUIRE(probe_contains("SPLR pl_trial_n=5 columns=42 periods=32"));
        REQUIRE(probe_contains("SPLR pl_trial_n=6 columns=87 periods=32"));
        for (u32 i = 0; i < probe_pl_count; i++) {
            const struct probe_pl_column *c = &probe_pl_columns[i];
            REQUIRE(c->row[0].count == c->n && c->row[1].count == c->n);
            REQUIRE(c->completed == PROBE_HPERIODS && !c->timeout && !c->early);
            for (u32 phase = 0; phase < 2; phase++) for (u32 k = 0; k < PROBE_PL_KINDS; k++) {
                const struct probe_pl_peak *p = &c->peak[phase][k];
                REQUIRE(p->batches == 64 && p->hits == 64 && !p->second_valid);
                if (k == PROBE_PL_W) REQUIRE(p->max - c->empty_min[phase] == c->w);
                if (k == PROBE_PL_D) REQUIRE(p->max == c->d);
            }
        }
        REQUIRE(irq_on);
        /* Observe the survey alone: the old player has its own IF tests. */
        test_open_at = 0; test_min_open = ~0ULL; test_max_closed = 0;
        probe_entry_costs(pc98_splash_raster_state());
        REQUIRE(probe_entry_upper > 0 && probe_retry_upper > 0);
        REQUIRE(test_min_open * den >= 100000);
        REQUIRE(test_max_closed * den <= 200000);
        /* Same survey under two successive interrupts: cumulative delay,
         * independent of Q/C/W calibration; no new ISR or CLI scan. */
        REQUIRE(!probe_phase());
        probe_entry_count = 0;
        irq_at[0] = scan_us + 20; irq_delay[0] = 11;
        irq_at[1] = scan_us + 40; irq_delay[1] = 17;
        probe_entries(pc98_splash_raster_state(), -(long long)den);
        REQUIRE(probe_entry[0].irq_us.max == 28);
        REQUIRE(probe_entry[0].irq_us.min >= 26);
        REQUIRE(probe_entry[0].frame_j_max <= -(long long)den);
        REQUIRE(test_min_open * den >= 100000);
        REQUIRE(test_max_closed * den <= 200000);
        /* Unknown high-width and out-of-tolerance periods are not adopted. */
        high_lines = 15;
        for (u32 i = 0; i <= 8; i++) {
            probe_edges[i].rise = (unsigned long long)i * total * 100000 / den;
            probe_edges[i].fall = probe_edges[i].rise + 15 * 100000 / den;
        }
        probe_classify(pc98_splash_raster_state()); REQUIRE(!probe_wave);
        probe_edges[8].rise += 10000;
        probe_classify(pc98_splash_raster_state()); REQUIRE(!probe_period_ok && !probe_wave);
    }
    setup(1, 1); initial_irq_us = 10000;
    platform_splash_probe();
    REQUIRE(!probe_errors && probe_entry[0].initial_margin < 0);
    REQUIRE(probe_contains("active_margin_num=-"));
    setup(1, 0); missing_h = 1;
    platform_splash_probe(); REQUIRE(probe_errors && irq_on);
    REQUIRE(probe_raw[0].bit[1].low == 32768 && !probe_raw[0].bit[1].high);
    REQUIRE(probe_raw[1].bit[1].low == 32768 && !probe_raw[1].bit[1].high);
    REQUIRE(probe_contains("SPLR h_reason=6"));
    REQUIRE(probe_contains("SPLR raw_port=160 blocks=8 reads=4096"));
    REQUIRE(probe_contains("SPLR raw_port=96 blocks=8 reads=4096"));
    setup(1, 0); frozen = 1;
    REQUIRE(probe_phase() < 0 && irq_on);
    finish(0);
}
