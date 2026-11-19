#include "pc98.h"
#include "serial_diag.h"
#include "pegc.h"
#include "os32_gui_shared.h"
#include "memmap.h"
#include "../../../gfx/gfx_dirty.h"
#include "gfx_hal.h"
#include "../../../gfx/v2/gfx2_int.h"
#include "display_restore.h"
#include "os32_kapi_shared.h"
#include "splash_raster.h"
#include "io.h"
#include "gfx_present_contract.h"

/* Compare with the last printed sample; a closed serial gate cannot consume it. */
void __attribute__((cold)) gfx_present_diag(void)
{
    static u32 previous;
    static u32 generation;
    if (g_backend != &gfx_backend_pegc || !gfx_init_gen || !bb_b || serial_gate_active()) return;
    volatile u8 *vram = P2V_IO(pegc_window_base());
    const u32 a = OS32_GUI_DIAG_P1_Y * PEGC_PITCH + OS32_GUI_DIAG_P1_X;
    const u32 b = OS32_GUI_DIAG_P2_Y * PEGC_PITCH + OS32_GUI_DIAG_P2_X;
    u32 sample = bb_b[a] | (u32)vram[a] << 8 |
                 (u32)bb_b[b] << 16 | (u32)vram[b] << 24;
    if (generation == gfx_init_gen && sample == previous) return;
    generation = gfx_init_gen; previous = sample;
    serial_puts_polled("[gfx] px n="); serial_diag_u32(gfx_counters.commits);
    for (u32 i = 0; i < 4; i++) {
        serial_puts_polled(i == 0 ? " a=" : i == 2 ? " b=" : "/");
        serial_diag_hex(sample & 255, 2); sample >>= 8;
    }
    serial_puts_polled("\r\n");
}

static void _flush_dirty_queue(void);
static void _merge_prev_dirty(void);
static void _flip_page(void);

/* ======================================================================== */
/*  ダーティレクタングルのVRAM転送 (VSYNC同期内等で呼ばれる)              */
/* ======================================================================== */
static void _flush_dirty_queue(void)
{
    int i, row;
    /* 表示メモリの 4 プレーンの番地は platform の provider が持つ (第 5 段 B6)。 */
    u8 *vb_base = display_planar_plane(0);
    u8 *vr_base = display_planar_plane(1);
    u8 *vg_base = display_planar_plane(2);
    u8 *vi_base = display_planar_plane(3);

    for (i = 0; i < dirty_queue.count; i++) {
        const GFX_Rect *r = gfx_dirty_at(&dirty_queue, i);
        int byte_x = r->x >> GVRAM_PIXELS_PER_BYTE_SHIFT;           /* 横は何バイト目か */
        int byte_w = r->w >> GVRAM_PIXELS_PER_BYTE_SHIFT;           /* 横幅のバイト数 */
        int words  = byte_w >> 1;         /* 16bit転送するワード数 */
        int physical_y;
        unsigned long phys_off, base_off;

        if (words <= 0) continue;

        /* 実 VRAM 書き込み量を数える (レビュー ⑦)。1 行 = words ワード ×2B ×4 プレーン、
         * ×r->h 行。ステイルページのマージ (prev_dirty) 分もここで正しく積まれる。 */
        gfx_counters.present_bytes += (u32)words * 2u * GVRAM_PLANES * (u32)r->h;

        physical_y = (r->y + vram_scroll_y) % gfx_current_height;
        phys_off = (unsigned long)physical_y * GFX_BPL + byte_x;
        base_off = (unsigned long)r->y * GFX_BPL + byte_x;

        for (row = 0; row < r->h; row++) {
            _memcpy_w(vb_base + phys_off, bb_b + base_off, words);
            _memcpy_w(vr_base + phys_off, bb_r + base_off, words);
            _memcpy_w(vg_base + phys_off, bb_g + base_off, words);
            _memcpy_w(vi_base + phys_off, bb_i + base_off, words);

            base_off += GFX_BPL;
            phys_off += GFX_BPL;
            physical_y++;
            if (physical_y >= gfx_current_height) {
                physical_y = 0;
                phys_off = byte_x;
            }
        }
    }
    dirty_queue.count = 0;
}

static void _merge_prev_dirty(void)
{
    gfx_dirty_merge(&dirty_queue, &prev_dirty, GFX_WIDTH, gfx_current_height);
}

/* ======================================================================== */
/*  ページ切替: 表示ページと描画ページを入れ替える (~3µs)              */
/* ======================================================================== */
static void _flip_page(void)
{
    (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R);
    gfx_display_page ^= 1;
    display_flip_pages(gfx_display_page, gfx_display_page ^ 1);
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_R, 0);
    gfx_counters.io_accesses += 2;   /* OUT ×2 (表示ページ + 描画ページ) */
}

void gfx_planar_present_dirty(int sync)
{
    if (gfx_flip_enabled) {
        DirtyRectQueue snapshot;
        if (dirty_queue.count == 0 && prev_dirty.count == 0) return;
        gfx_counters.commits++;
        /* 今フレームのオリジナルdirtyを保存 (マージ前) */
        snapshot = dirty_queue;
        _merge_prev_dirty();
        _flush_dirty_queue();
        prev_dirty = snapshot;
        _flip_page();
    } else {
        if (dirty_queue.count == 0) return;
        gfx_counters.commits++;
        if (sync) {
            /* VSYNC期間になるまで待つ */
            display_wait_vsync();
        }
        _flush_dirty_queue();
    }
}

void gfx_planar_present_raster(GFX_RasterPalTable *table)
{
    gfx_counters.commits++;
    if (gfx_flip_enabled) {
        if (dirty_queue.count > 0 || prev_dirty.count > 0) {
            DirtyRectQueue snapshot = dirty_queue;
            _merge_prev_dirty();
            _flush_dirty_queue();
            prev_dirty = snapshot;
            _flip_page();
        }
    } else {
        _flush_dirty_queue();
    }

    (void)pc98_splash_raster_table(pc98_splash_raster_state(), table);
}
