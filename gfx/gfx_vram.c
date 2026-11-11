#include "serial_diag.h"
#include "pegc.h"
#include "os32_gui_shared.h"
#include "memmap.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "v2/gfx2_int.h"
#include "display_restore.h"
#include "os32_kapi_shared.h"
#include "../drivers/video/pc98/splash_raster.h"
#include "io.h"

/* Present reads the CLIENT and writes the DISPLAY synchronously. */
static void gfx_v1_present_rect(int x, int y, int w, int h)
{
    (void)v1_bridge_begin(GFX2_ROLE_CLIENT, GFX2_CPU_R);
    (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W);
    g_backend->present_rect(x, y, w, h);
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_W, 0);
    v1_bridge_end(GFX2_ROLE_CLIENT, GFX2_CPU_R, 0);
}

/* Compare with the last printed sample; a closed serial gate cannot consume it. */
static void __attribute__((cold)) gfx_diag_px(void)
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
/*  パックド系バックエンド (PEGC 256 色 / H2) 用のダーティ転送               */
/*                                                                          */
/*  _flush_dirty_queue 以下は 4 プレーンの VRAM (0xA8000 系) を前提にした    */
/*  9801 専用の転送エンジン。パックド 8bpp ではプレーンが無く、しかも        */
/*  拡張グラフィックモードでは 0xA8000 が 32KB のバンク窓に化けているので、  */
/*  同じ経路を走らせると画面が壊れる。gdi_test / gshell の WM は            */
/*  gfx_present_dirty() を直接呼ぶ (KAPI) ため、ここで受けてバックエンドの   */
/*  present_rect へ流す。                                                    */
/*                                                                          */
/*  戻り値: 1 = パックド系だったので処理した (呼び出し元は即 return)。       */
/* ======================================================================== */
static int _present_dirty_packed(void)
{
    u32 commits_before;
    int i;

    if (!g_backend || g_backend->bb_format == GFX_BB_PLANAR4) return 0;
    if (!g_backend->present_rect) return 1;
    if (dirty_queue.count == 0) return 1;

    /* present_rect は 1 回ごとに commits を数えるが、契約 G7 の commits は
     * 「present (commit) 回数」なので、この 1 回ぶんに揃える。 */
    commits_before = gfx_counters.commits;
    for (i = 0; i < dirty_queue.count; i++) {
        GFX_Rect *r = &dirty_queue.rects[i];
        gfx_v1_present_rect(r->x, r->y, r->w, r->h);
    }
    gfx_counters.commits = commits_before + 1;

    dirty_queue.count = 0;
    prev_dirty.count = 0;   /* ページフリップが無いのでステイルページも無い */
    return 1;
}

/* ======================================================================== */
/*  KAPI: ダーティレクタングルの追加 (OS管理)                              */
/* ======================================================================== */
void __cdecl gfx_add_dirty_rect(int x, int y, int w, int h)
{
    if (!bb_b) return;
    int aligned_x, aligned_w;
    int right;
    int i;
    GFX_Rect *r;

    /* マージ判定用のギャップ閾値 (px)。
     * この距離以内の矩形は統合する (32pxアライメントに合わせる) */
    int gap = BITS_PER_U32;

    int new_x, new_y, new_r, new_b;

    if (!gfx_clip_screen(&x, &y, &w, &h, GFX_WIDTH, gfx_current_height)) return;
    right = x + w;

    /* 32ピクセル境界 (4バイト) にアライメント:
     * xは切り捨て、幅は切り上げ
     */
    aligned_x = x & ~(BITS_PER_U32 - 1);
    aligned_w = ((right + (BITS_PER_U32 - 1)) & ~(BITS_PER_U32 - 1)) - aligned_x;

    /* --- マージ判定 --- */
    new_x = aligned_x;
    new_y = y;
    new_r = aligned_x + aligned_w;
    new_b = y + h;

    for (i = 0; i < dirty_queue.count; i++) {
        int ex, ey, er, eb;

        r = &dirty_queue.rects[i];
        ex = r->x;
        ey = r->y;
        er = ex + r->w;
        eb = ey + r->h;

        /* 完全包含チェック: 新rectが既存rectに包含されるなら何もしない */
        if (new_x >= ex && new_y >= ey && new_r <= er && new_b <= eb) {
            return;
        }

        /* オーバーラップ or 隣接(gap px以内)なら統合 */
        if (new_x <= er + gap && new_r >= ex - gap &&
            new_y <= eb + gap && new_b >= ey - gap) {
            /* バウンディングボックスに統合 */
            if (new_x < ex) r->x = new_x; else new_x = ex;
            if (new_y < ey) r->y = new_y; else new_y = ey;
            if (new_r > er) er = new_r;
            if (new_b > eb) eb = new_b;
            r->w = er - r->x;
            r->h = eb - r->y;
            return;
        }
    }

    /* マージ対象なし → 新規追加 */

    /* キューがいっぱいの場合はバウンディングボックスに圧縮 */
    if (dirty_queue.count >= MAX_DIRTY_RECTS) {
        int min_x = GFX_WIDTH, min_y = gfx_current_height, max_x = 0, max_y = 0;
        for (i = 0; i < dirty_queue.count; i++) {
            if (dirty_queue.rects[i].x < min_x) min_x = dirty_queue.rects[i].x;
            if (dirty_queue.rects[i].y < min_y) min_y = dirty_queue.rects[i].y;
            if (dirty_queue.rects[i].x + dirty_queue.rects[i].w > max_x) max_x = dirty_queue.rects[i].x + dirty_queue.rects[i].w;
            if (dirty_queue.rects[i].y + dirty_queue.rects[i].h > max_y) max_y = dirty_queue.rects[i].y + dirty_queue.rects[i].h;
        }
        dirty_queue.count = 1;
        dirty_queue.rects[0].x = min_x;
        dirty_queue.rects[0].y = min_y;
        dirty_queue.rects[0].w = max_x - min_x;
        dirty_queue.rects[0].h = max_y - min_y;
    }

    r = &dirty_queue.rects[dirty_queue.count++];
    r->x = new_x;
    r->y = new_y;
    r->w = new_r - new_x;
    r->h = new_b - new_y;
}

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
        GFX_Rect *r = &dirty_queue.rects[i];
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

/* ======================================================================== */
/*  ステイルページ対策: 前フレームのdirty rectを現フレームにマージ            */
/* ======================================================================== */
static void _merge_prev_dirty(void)
{
    int i;
    for (i = 0; i < prev_dirty.count; i++) {
        GFX_Rect *r = &prev_dirty.rects[i];
        gfx_add_dirty_rect(r->x, r->y, r->w, r->h);
    }
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

static void _present_dirty(int sync)
{
    if (!bb_b) return;
    if (_present_dirty_packed()) return;
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

/* ======================================================================== */
/*  KAPI: 強制でダーティ領域をVSYNC同期転送する                           */
/*  フリップモード時はVSYNC待ちなしで転送してページ切替。                 */
/* ======================================================================== */
void __cdecl gfx_present_dirty(void)
{
    u32 before = gfx_counters.present_bytes;
    _present_dirty(1);
    gfx_diag_first_present(before);
    gfx_diag_px();
}

/* ======================================================================== */
/*  KAPI: VSYNC待ちなしでダーティ領域を即座にVRAM転送する                  */
/*                                                                          */
/*  フリップモード時: gfx_present_dirty()と同じ動作 (元々VSYNC不要)。    */
/*  従来モード時: VSYNC待ちなしで即座に転送 (従来動作維持)。           */
/* ======================================================================== */
void __cdecl gfx_present_nosync(void)
{
    u32 before = gfx_counters.present_bytes;
    _present_dirty(0);
    gfx_diag_first_present(before);
}

/* ======================================================================== */
/*  互換KAPI実装 (OS管理に寄せる)                                          */
/* ======================================================================== */
void __cdecl gfx_present(void)
{
    u32 before = gfx_counters.present_bytes;
    if (!bb_b) goto diag;
    /* HAL バックエンド経由 (契約 G4 の present_rect)。全画面 present。 */
    if (g_backend && g_backend->present_rect)
        gfx_v1_present_rect(0, 0, GFX_WIDTH, gfx_current_height);
diag:
    gfx_diag_first_present(before);
    gfx_diag_px();
}

void __cdecl gfx_present_rect(int rx, int ry, int rw, int rh)
{
    u32 before = gfx_counters.present_bytes;
    if (!bb_b) goto diag;
    if (g_backend && g_backend->present_rect)
        gfx_v1_present_rect(rx, ry, rw, rh);
diag:
    gfx_diag_first_present(before);
    gfx_diag_px();
}

/* ======================================================================== */
/*  KAPI: ラスタパレット付きVRAM転送                                        */
/*                                                                          */
/*  dirty は先に一括転送し、flip 後の次の VSYNC から共通再生器へ渡す。       */
/*  line=0 は初期色、以後は可視行の絶対位置 (異なる行の間隔は 2 行以上)。   */
/*  再生器が目標の 1 行前から HBLANK の 4 OUT までだけ IF=0 にし、           */
/*  各保護区間後は IF=1 を 1 行以上確保する。VRAM 転送は保護しない。        */
/*  入口 IF=0・判定不可・同期失敗・予算違反では転送済み像を静止提示する。   */
/* ======================================================================== */
void __cdecl gfx_present_raster(GFX_RasterPalTable *table)
{
    u32 before = gfx_counters.present_bytes;
    if (!bb_b) goto diag;
    if (!table || table->count <= 0 || table->count > GFX_RASTER_MAX_ENTRIES) goto diag;

    /* packed 系は通常の dirty present に委譲する。 */
    if (_present_dirty_packed()) goto diag;

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
diag:
    gfx_diag_first_present(before);
}
