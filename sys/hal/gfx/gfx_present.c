#include "gfx_dirty.h"
#include "gfx_hal.h"
#include "v2/gfx2_int.h"
#include "display_provider.h"
#include "gfx_present_contract.h"

/* Present reads the CLIENT and writes the DISPLAY synchronously. */
static void gfx_v1_present_rect(int x, int y, int w, int h)
{
    (void)v1_bridge_begin(GFX2_ROLE_CLIENT, GFX2_CPU_R);
    (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W);
    u32 before = gfx_counters.present_bytes;
    int packed = g_backend->bb_format != GFX_BB_PLANAR4;
    if (packed)
        GFX_TRACE(GFX_TRACE_PRESENT, gfx_counters.commits, x, y, w, h, 0, 0);
    g_backend->present_rect(x, y, w, h);
    if (packed)
        GFX_TRACE(GFX_TRACE_PACKED, gfx_counters.commits, x, y, w, h,
                  gfx_counters.present_bytes - before, 0);
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_W, 0);
    v1_bridge_end(GFX2_ROLE_CLIENT, GFX2_CPU_R, 0);
}

/* ======================================================================== */
/*  パックド系バックエンド (PEGC 256 色 / H2) 用のダーティ転送               */
/*                                                                          */
/*  gfx_vram.c の _flush_dirty_queue は 4 プレーンの VRAM (0xA8000 系) を前提にした    */
/*  9801 専用の転送エンジン。パックド 8bpp ではプレーンが無く、しかも        */
/*  拡張グラフィックモードでは 0xA8000 が 32KB のバンク窓に化けているので、  */
/*  同じ経路を走らせると画面が壊れる。gdi_test / gshell の WM は            */
/*  gfx_present_dirty() を直接呼ぶ (KAPI) ため、ここで受けてバックエンドの   */
/*  present_rect へ流す。                                                    */
/*                                                                          */
/*  戻り値: 1 = パックド系だったので処理した (呼び出し元は即 return)。       */
/* ======================================================================== */
static int present_dirty_packed(void)
{
    u32 commits_before;
    int i;

    if (!g_backend || g_backend->bb_format == GFX_BB_PLANAR4) return 0;
    if (!g_backend->present_rect) return 1;
    if (dirty_queue.count == 0) return 1;

    /* present_rect は 1 回ごとに commits を数えるが、契約 G7 の commits は
     * 「present (commit) 回数」なので、この 1 回ぶんに揃える。 */
    GFX_TRACE(GFX_TRACE_FRAME, gfx_counters.commits, 0, 0, 0, 0, 0, 0);
    commits_before = gfx_counters.commits;
    int policy = gfx_trace_present_policy(1);
    u32 generation = gfx_init_gen;
    GfxBackend *backend = g_backend;
    int full = policy == GFX_TRACE_REDRAW;
    int count = full ? 1 : dirty_queue.count;
    for (i = 0; i < count; i++) {
        const GFX_Rect *r = gfx_dirty_at(&dirty_queue, i);
        u32 previous = gfx_counters.commits;
        if (policy != GFX_TRACE_FAIL_ONCE) {
            if (full) gfx_v1_present_rect(0, 0, DISPLAY_V1_WIDTH, gfx_current_height);
            else gfx_v1_present_rect(r->x, r->y, r->w, r->h);
        }
        if (g_backend != backend || gfx_init_gen != generation) return 1;
        if (gfx_counters.commits == previous) {
            /* The void backend publishes completion in commits. Preserve this
             * rectangle (including any partial write) and the unvisited tail. */
            GFX_TRACE(GFX_TRACE_FAILED, previous, r->x, r->y, r->w, r->h, 0, 0);
            if (i) {
                dirty_queue.count -= i;
                for (int j = 0; j < dirty_queue.count; j++)
                    dirty_queue.rects[j] = dirty_queue.rects[j + i];
            }
            gfx_counters.commits = commits_before;
            return 1;
        }
    }
    GFX_TRACE(GFX_TRACE_FLIP, gfx_counters.commits, 0, 0, DISPLAY_V1_WIDTH,
              gfx_current_height, 0, 0);
    gfx_counters.commits = commits_before + 1;

    dirty_queue.count = 0;
    prev_dirty.count = 0;   /* ページフリップが無いのでステイルページも無い */
    return 1;
}

void __cdecl gfx_add_dirty_rect(int x, int y, int w, int h)
{
    if (bb_b && DISPLAY_V1_PRESENT_OK()) gfx_dirty_add(&dirty_queue, DISPLAY_V1_WIDTH, gfx_current_height, x, y, w, h);
}

static void present_dirty(int sync)
{
    if (!bb_b) return;
    if (present_dirty_packed()) return;
    gfx_planar_present_dirty(sync);
}

/* ======================================================================== */
/*  KAPI: 強制でダーティ領域をVSYNC同期転送する                           */
/*  フリップモード時はVSYNC待ちなしで転送してページ切替。                 */
/* ======================================================================== */
void __cdecl gfx_present_dirty(void)
{
    u32 before = gfx_counters.present_bytes;
    if (bb_b && !DISPLAY_V1_PRESENT_OK()) return;
    present_dirty(1);
    gfx_diag_first_present(before);
    gfx_present_diag();
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
    if (bb_b && !DISPLAY_V1_PRESENT_OK()) return;
    present_dirty(0);
    gfx_diag_first_present(before);
}

/* ======================================================================== */
/*  互換KAPI実装 (OS管理に寄せる)                                          */
/* ======================================================================== */
void __cdecl gfx_present(void)
{
    u32 before = gfx_counters.present_bytes;
    if (!bb_b) goto diag;
    if (!DISPLAY_V1_PRESENT_OK()) return;
    /* HAL バックエンド経由 (契約 G4 の present_rect)。全画面 present。 */
    if (g_backend && g_backend->present_rect) {
        if (g_backend->bb_format != GFX_BB_PLANAR4)
            GFX_TRACE(GFX_TRACE_FRAME, gfx_counters.commits, 0, 0, 0, 0, 0, 0);
        gfx_v1_present_rect(0, 0, DISPLAY_V1_WIDTH, gfx_current_height);
        if (g_backend && g_backend->bb_format != GFX_BB_PLANAR4)
            GFX_TRACE(GFX_TRACE_FLIP, gfx_counters.commits, 0, 0, DISPLAY_V1_WIDTH,
                      gfx_current_height, 0, 0);
    }
diag:
    gfx_diag_first_present(before);
    gfx_present_diag();
}

void __cdecl gfx_present_rect(int rx, int ry, int rw, int rh)
{
    u32 before = gfx_counters.present_bytes;
    if (!bb_b) goto diag;
    if (!DISPLAY_V1_PRESENT_OK()) return;
    if (g_backend && g_backend->present_rect) {
        if (g_backend->bb_format != GFX_BB_PLANAR4)
            GFX_TRACE(GFX_TRACE_FRAME, gfx_counters.commits, 0, 0, 0, 0, 0, 0);
        if (g_backend->bb_format != GFX_BB_PLANAR4 && gfx_trace_present_policy(0) == GFX_TRACE_REDRAW)
            gfx_v1_present_rect(0, 0, DISPLAY_V1_WIDTH, gfx_current_height);
        else
            gfx_v1_present_rect(rx, ry, rw, rh);
        if (g_backend && g_backend->bb_format != GFX_BB_PLANAR4)
            GFX_TRACE(GFX_TRACE_FLIP, gfx_counters.commits, 0, 0, DISPLAY_V1_WIDTH,
                      gfx_current_height, 0, 0);
    }
diag:
    gfx_diag_first_present(before);
    gfx_present_diag();
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
    if (!DISPLAY_V1_PRESENT_OK()) return;
    if (!table || table->count <= 0 || table->count > GFX_RASTER_MAX_ENTRIES) goto diag;

    /* packed 系は通常の dirty present に委譲する。 */
    if (present_dirty_packed()) goto diag;

    gfx_planar_present_raster(table);
diag:
    gfx_diag_first_present(before);
}
