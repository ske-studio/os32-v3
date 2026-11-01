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

struct probe_edge { unsigned long long rise, fall, text, text_fall; };
struct probe_frame {
    unsigned long long duration, closed_max, closed_total;
    u32 late, dropped, unapplied, tick;
};
static struct probe_edge probe_edges[PROBE_FRAMES + 1];
static struct probe_frame probe_frames[PROBE_FRAMES];
static unsigned long long probe_hperiod[PROBE_HPERIODS], probe_hwidth[PROBE_HPERIODS];
static unsigned long long probe_clock[PROBE_SAMPLES];
static u32 probe_errors, probe_raw_before, probe_bios_before;
static u32 probe_rx_count, probe_rx_missing, probe_rx_value, probe_rx_bytes, probe_rx_next;
static unsigned long long probe_previous;
static GFX_RasterPalTable probe_table;

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
    unsigned long long stamp, previous, io[3][3] = {{0}}, empty[3] = {0}, initial_begin = 0, initial_end = 0, initial_fall = 0;
    u32 initial_status = 0, completed = 0, hcompleted = 0, missed_h = 0;
    const u32 counts[3] = {1, 4, 8};
    if (serial_gate_active() || !_irq_enabled()) return;
    serial_get_status(&mode, &baud, &fifo);
    serial_diag_get(&oe0, &fe0, &pe0, &ov0);
    probe_errors = probe_rx_count = probe_rx_missing = probe_rx_bytes = probe_rx_value = probe_rx_next = 0;
    probe_previous = 0;
    for (u32 i = 0; i < PROBE_FRAMES; i++) probe_frames[i] = (struct probe_frame){0};
    for (u32 i = 0; i <= PROBE_FRAMES; i++) probe_edges[i] = (struct probe_edge){0};
    probe_pattern();
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
    if (probe_bit(GDC_GFX_STAT, GDC_STAT_HBLANK, 0, &stamp) ||
        probe_bit(GDC_GFX_STAT, GDC_STAT_HBLANK, 1, &previous)) goto dump;
    for (u32 i = 0; i < PROBE_HPERIODS; i++) {
        unsigned long long end;
        if (probe_bit(GDC_GFX_STAT, GDC_STAT_HBLANK, 0, &end) ||
            probe_bit(GDC_GFX_STAT, GDC_STAT_HBLANK, 1, &stamp)) break;
        probe_hwidth[i] = end - previous;
        probe_hperiod[i] = stamp - previous;
        if (s->line_den && probe_hperiod[i] * s->line_den > 2 * s->line_num_us) missed_h++;
        previous = stamp; hcompleted++;
    }
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
    initial_begin = probe_now();
    for (u32 i = 0; i < OSRI_COLORS; i++) palette_set(i, i, i, i);
    initial_end = probe_now(); initial_status = inp(GDC_GFX_STAT);
    if (probe_bit(GDC_GFX_STAT, GDC_STAT_VSYNC, 0, &stamp)) goto dump;
    initial_fall = stamp;
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
    probe_field(" active_margin_us=", initial_fall +
        (s->line_den ? (unsigned long long)s->active0_lines * s->line_num_us / s->line_den : 0) - initial_end);
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
