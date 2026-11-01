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
static u32 den, total, high_lines, raw_mode, wide_h;
static int irq_on = 1, missing_h, frozen, backwards;
static u32 initial_irq_us;
static u32 calibration_irq_us;
static u32 if0_end_clock, if0_in_reads, if0_max_in_run;
static int require_clockless_in;
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
    if (++calls > 8000000) finish(__LINE__);
    if (!frozen) scan_us += cost;
    for (u32 i = 0; i < 4; i++) if (irq_on && irq_delay[i] && scan_us >= irq_at[i]) {
        scan_us += irq_delay[i]; irq_delay[i] = 0;
    }
}
int sys_time_now(u32 *lo, u32 *hi)
{
    if (!irq_on && if0_in_reads && require_clockless_in) if0_end_clock++;
    advance(clock_cost_us);
    unsigned long long t = backwards ? 10000000 - scan_us : scan_us;
    *lo = t; *hi = t >> BITS_PER_U32;
    return 0;
}
static unsigned int inp(unsigned int port)
{
    u32 status = 0;
    if (port == PEGC_HSYNC_PORT) status = raw_mode;
    else {
        REQUIRE(port == GDC_GFX_STAT || port == GDC_TEXT_STAT);
        unsigned long long phase = scan_us * den % ((unsigned long long)total * 100000);
        if (phase < (unsigned long long)high_lines * 100000) status |= GDC_STAT_VSYNC;
        u32 width = wide_h ? (raw_mode ? 80000 : 75472) : (raw_mode ? 20000 : 24532);
        if (!missing_h && phase % 100000 >= 100000 - width) status |= GDC_STAT_HBLANK;
    }
    advance(in_us); /* sample and return are deliberately distinct */
    if (!irq_on) {
        if (require_clockless_in) REQUIRE(!if0_end_clock);
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
#include "../../platform/pc98/splash_probe.c"
#undef pc98_splash_raster_table

volatile u32 tick_count;
u32 ktime_clamp_count;
const char os32_build_commit[] = "host-probe";
static struct bootinfo probe_bootinfo = { .img_crc = 123, .source = 2 };
static struct pit_setup probe_pit = { .reload = 24576 };
static int probe_presents, probe_rects, probe_rx_pos;
static u32 probe_checked_frames;
static char probe_output[131072];
static u32 probe_output_size;
int gfx_current_height = 400, gfx_flip_enabled = 1;
const struct bootinfo *bootinfo_get(void) { return &probe_bootinfo; }
const struct pit_setup *pit_get_setup(void) { return &probe_pit; }
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *p)
{
    REQUIRE(probe_frames[PROBE_FRAMES-1].duration || probe_errors);
    REQUIRE(irq_on);
    while (*p) { REQUIRE(probe_output_size < sizeof(probe_output)-1); probe_output[probe_output_size++] = *p++; }
    probe_output[probe_output_size] = 0;
}
int serial_get_status(u32 *mode, u32 *baud, u32 *fifo)
{ *mode = 0; *baud = 9600; *fifo = 1; return 0; }
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
    if (index == 0 && initial_irq_us) {
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
    scan_us = 0; den = mode ? 3147 : 2483; total = mode ? 449 : 440;
    high_lines = vs ? (mode ? 2 : 8) : total - 400;
    raw_mode = mode; wide_h = 0;
    irq_on = 1; missing_h = frozen = backwards = 0;
    calls = writes = probe_errors = 0; probe_previous = 0;
    probe_output_size = probe_presents = probe_rects = probe_rx_pos = 0;
    probe_bios_reads = 0;
    probe_bios_snapshot[PEGC_BIOS_PRXCRT] = mode ? PEGC_BIOS_PRXCRT_31KHZ : 0;
    probe_checked_frames = 0;
    initial_irq_us = 0;
    calibration_irq_us = 0;
    if0_end_clock = if0_in_reads = if0_max_in_run = 0;
    require_clockless_in = 0;
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
        REQUIRE(probe_budget.b.min >= reference - 2 * (long long)den);
        REQUIRE(probe_budget.b.max <= reference + 2 * (long long)den);
        unsigned long long highs = 0, samples = 0;
        for (u32 i = 0; i < PROBE_HPERIODS; i++) {
            highs += probe_highsamples[i]; samples += probe_hsamples[i];
        }
        long long measured = highs * 100000 / samples;
        REQUIRE(probe_delta(measured, reference) <= (unsigned long long)den);
        REQUIRE(test_max_closed * den <= 200000);
        REQUIRE(test_min_open * den >= 100000);
        REQUIRE(!probe_errors && irq_on);
    }
    in_us = clock_cost_us = out_us = 1;
}
void _start(void)
{
    /* L1: a 5us clock must not turn the 31k 6.36us pulse into B_min=0. */
    setup(1, 0); in_us = 1; clock_cost_us = 5; out_us = 1;
    platform_splash_probe();
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
    costs(); budgets(); widths();
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
        REQUIRE(probe_contains("sample=clock_read_us n=128"));
        REQUIRE(probe_contains("sample=vsync_us n=32"));
        REQUIRE(probe_contains("sample=hperiod_us n=64"));
        REQUIRE(probe_contains(" begin_us=") && probe_contains(" end_us="));
        REQUIRE(probe_contains("errors=0") && probe_contains("rx_words=8"));
        REQUIRE(probe_contains("commit=host-probe crc=123"));
        REQUIRE(probe_contains("SPLR cost=0 num_us=512 den=512 upper_us=2 upper_num_us=73 upper_den=64"));
        REQUIRE(probe_contains("SPLR entry_y=396"));
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
    setup(1, 0); frozen = 1;
    REQUIRE(probe_phase() < 0 && irq_on);
    finish(0);
}
