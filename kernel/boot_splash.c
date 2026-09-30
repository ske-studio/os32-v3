/* ======================================================================== */
/*  BOOT_SPLASH.C - OS32 ブートスプラッシュ画面 (カーネル内蔵)               */
/*                                                                          */
/*  3Dエンボス風「OS32」ロゴの背景にラスタパレットによる虹色グラデーション      */
/*  スクロールアニメーションを3秒間表示する。                                  */
/*                                                                          */
/*  カーネルのGFXサブシステムとバックバッファを直接操作する。                  */
/*  GFX初期化→描画→アニメーション→VRAMクリア→テキストモード復帰を完結。     */
/* ======================================================================== */

#include "gfx.h"
#include "gfx_internal.h"
#include "gfx_hal.h"   /* g_backend / GFX_BB_*: 4 プレーン機かどうかの判定 */
#include "palette.h"
#include "tvram.h"
#include "types.h"
#include "os32_kapi_shared.h"
#include "io.h"
#include "kstring.h"

/* ======== 外部参照 ======== */
extern volatile u32 tick_count;
extern u8 *bb[4];  /* バックバッファプレーン [B,R,G,I] */
extern void __cdecl gfx_add_dirty_rect(int x, int y, int w, int h);

/* ======== パレット番号 ======== */
#define PAL_BG          0
#define PAL_GRAD        1
#define PAL_OUTLINE     2
#define PAL_FACE        7
#define PAL_SHADOW      8
#define PAL_HIGHLIGHT   15

/* ======== 3Dエフェクト定数 ======== */
#define SHADOW_OFS      4
#define OUTLINE_PAD     2
#define HL_WIDTH        3

/* ======== タイムアウト ======== */
#define BOOT_TIMEOUT    300     /* 100Hz x 3秒 */

/* ======== グラデーション ======== */
#define GRAD_STEPS      100

static u8 s_grad_r[GRAD_STEPS];
static u8 s_grad_g[GRAD_STEPS];
static u8 s_grad_b[GRAD_STEPS];

/* ======================================================================== */
/*  バックバッファ描画プリミティブ (カーネル用)                               */
/* ======================================================================== */

/* バックバッファ全面を指定色で塗る — kmemset (rep stosd) で高速化 */
static void bb_clear(u8 color)
{
    int p;
    for (p = 0; p < 4; p++) {
        u8 val = (color & (1 << p)) ? 0xFF : 0x00;
        kmemset(bb[p], val, GFX_PLANE_SZ);
    }
}

/* バックバッファに矩形を塗る */
static void bb_fill_rect(int x, int y, int w, int h, u8 color)
{
    int p, row;
    int x1, x2, byte_start, byte_end;
    u8 mask_first, mask_last;

    /* クリッピング */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_WIDTH) w = GFX_WIDTH - x;
    if (y + h > GFX_HEIGHT) h = GFX_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    x1 = x;
    x2 = x + w - 1;
    byte_start = x1 / 8;
    byte_end   = x2 / 8;
    mask_first = 0xFF >> (x1 & 7);
    mask_last  = 0xFF << (7 - (x2 & 7));

    for (p = 0; p < 4; p++) {
        u8 *plane = bb[p];
        u8 set = (color & (1 << p)) ? 1 : 0;

        for (row = y; row < y + h; row++) {
            u8 *line = plane + row * GFX_BPL;

            if (byte_start == byte_end) {
                u8 mask = mask_first & mask_last;
                if (set)
                    line[byte_start] |= mask;
                else
                    line[byte_start] &= ~mask;
            } else {
                int b;
                if (set)
                    line[byte_start] |= mask_first;
                else
                    line[byte_start] &= ~mask_first;

                for (b = byte_start + 1; b < byte_end; b++)
                    line[b] = set ? 0xFF : 0x00;

                if (set)
                    line[byte_end] |= mask_last;
                else
                    line[byte_end] &= ~mask_last;
            }
        }
    }
}

/* ======================================================================== */
/*  グリフデータ                                                             */
/*  各文字: 幅120px x 高さ170px  ストローク幅: ~26px                         */
/* ======================================================================== */

typedef struct {
    i16 x, y, w, h;
} SplashGlyph;

static const SplashGlyph glyph_O[] = {
    {24,   0, 72, 26},
    { 0,  20, 26, 130},
    {94,  20, 26, 130},
    {24, 144, 72, 26}
};
#define GLYPH_O_N  4

static const SplashGlyph glyph_S[] = {
    {16,   0, 104, 26},
    { 0,  20,  26, 58},
    {16,  72,  88, 26},
    {94,  92,  26, 58},
    { 0, 144, 104, 26}
};
#define GLYPH_S_N  5

static const SplashGlyph glyph_3[] = {
    { 0,   0, 104, 26},
    {94,  20,  26, 58},
    {16,  72,  88, 26},
    {94,  92,  26, 58},
    { 0, 144, 104, 26}
};
#define GLYPH_3_N  5

static const SplashGlyph glyph_2[] = {
    {16,   0,  88, 26},
    {94,  20,  26, 58},
    {16,  72,  88, 26},
    { 0,  92,  26, 58},
    { 0, 144, 120, 26}
};
#define GLYPH_2_N  5

/* ======== 文字配置 ======== */
#define CHAR_W    120
#define CHAR_H    170
#define CHAR_GAP  16
#define NUM_CHARS 4
#define TOTAL_TXT_W  (NUM_CHARS * CHAR_W + (NUM_CHARS - 1) * CHAR_GAP)
#define TEXT_X    ((GFX_WIDTH - TOTAL_TXT_W) / 2)
#define TEXT_Y    ((GFX_HEIGHT - CHAR_H) / 2)

/* ======================================================================== */
/*  HSV to RGB (H:0-360, S/V:0-15)                                          */
/* ======================================================================== */
static void splash_hsv(int h, int s, int v, u8 *r, u8 *g, u8 *b)
{
    int hi, f, p, q, t;
    if (s == 0) { *r = *g = *b = (u8)v; return; }
    hi = (h / 60) % 6;
    f = h % 60;
    p = v * (15 - s) / 15;
    q = v * (15 - (s * f / 60)) / 15;
    t = v * (15 - (s * (60 - f) / 60)) / 15;
    switch (hi) {
        case 0: *r = (u8)v; *g = (u8)t; *b = (u8)p; break;
        case 1: *r = (u8)q; *g = (u8)v; *b = (u8)p; break;
        case 2: *r = (u8)p; *g = (u8)v; *b = (u8)t; break;
        case 3: *r = (u8)p; *g = (u8)q; *b = (u8)v; break;
        case 4: *r = (u8)t; *g = (u8)p; *b = (u8)v; break;
        default:*r = (u8)v; *g = (u8)p; *b = (u8)q; break;
    }
}

static void make_grad_table(void)
{
    int i, h;
    for (i = 0; i < GRAD_STEPS; i++) {
        h = i * 360 / GRAD_STEPS;
        splash_hsv(h, 15, 15, &s_grad_r[i], &s_grad_g[i], &s_grad_b[i]);
    }
}

/* ======================================================================== */
/*  グリフ描画                                                               */
/* ======================================================================== */

static void draw_rects(int ox, int oy,
                       const SplashGlyph *r, int n, u8 col)
{
    int i;
    for (i = 0; i < n; i++)
        bb_fill_rect(ox + r[i].x, oy + r[i].y, r[i].w, r[i].h, col);
}

static void draw_outline(int ox, int oy,
                          const SplashGlyph *r, int n, u8 col, int pad)
{
    int i;
    for (i = 0; i < n; i++)
        bb_fill_rect(ox + r[i].x - pad, oy + r[i].y - pad,
                     r[i].w + pad * 2, r[i].h + pad * 2, col);
}

static void draw_highlight(int ox, int oy,
                            const SplashGlyph *r, int n, u8 col, int w)
{
    int i;
    for (i = 0; i < n; i++) {
        bb_fill_rect(ox + r[i].x, oy + r[i].y, r[i].w, w, col);
        bb_fill_rect(ox + r[i].x, oy + r[i].y, w, r[i].h, col);
    }
}

static void draw_char_3d(int ox, int oy,
                          const SplashGlyph *r, int n)
{
    draw_rects(ox + SHADOW_OFS, oy + SHADOW_OFS, r, n, PAL_SHADOW);
    draw_outline(ox, oy, r, n, PAL_OUTLINE, OUTLINE_PAD);
    draw_rects(ox, oy, r, n, PAL_FACE);
    draw_highlight(ox, oy, r, n, PAL_HIGHLIGHT, HL_WIDTH);
}

/* ======================================================================== */
/*  ラスタテーブル構築                                                        */
/* ======================================================================== */
static void build_raster(GFX_RasterPalTable *tbl, int offset)
{
    int step, idx;
    tbl->count = 0;
    for (step = 0; step < GRAD_STEPS; step++) {
        GFX_RasterPalEntry *e;
        if (tbl->count >= GFX_RASTER_MAX_ENTRIES) break;
        idx = (step + offset) % GRAD_STEPS;
        e = &tbl->entries[tbl->count];
        e->line = (u16)(step * 4);
        e->pal_idx = PAL_GRAD;
        e->r = s_grad_r[idx] & 0x0F;
        e->g = s_grad_g[idx] & 0x0F;
        e->b = s_grad_b[idx] & 0x0F;
        tbl->count++;
    }
}

/* ブートでは必ず存在する PC98 標準表示だけを使う。任意デバイスの probe /
 * init は後の通常 gfx_init に任せ、GFX= の希望値は描画前に戻す。
 * 起動中の同期呼び出し専用。設定ファイルや公開 ABI は変更しない。 */
static void splash_gfx_init(void)
{
    int saved_pref = gfx_get_backend_pref();
    gfx_set_backend_pref(GFX_PREF_PC98);
    gfx_init();
    gfx_set_backend_pref(saved_pref);
}

/* ======================================================================== */
/*  boot_splash - メインエントリ                                              */
/* ======================================================================== */
void boot_splash(void)
{
    int cx, i, grad_offset;
    u32 start_tick;
    GFX_RasterPalTable raster_table;

    const SplashGlyph *gdata[NUM_CHARS];
    int gcount[NUM_CHARS];

    gdata[0] = glyph_O;  gcount[0] = GLYPH_O_N;
    gdata[1] = glyph_S;  gcount[1] = GLYPH_S_N;
    gdata[2] = glyph_3;  gcount[2] = GLYPH_3_N;
    gdata[3] = glyph_2;  gcount[3] = GLYPH_2_N;

    /* GUI の設定とは独立に、任意デバイスを probe しない標準表示で起動。 */
    splash_gfx_init();

    /* 標準ハードの存在とソフトウェア状態の正常性は別。gfx_init は void
     * なので、描画に必要な状態が無ければ任意デバイスへ逃げず表示を終了。
     * 初期化中の fault や I/O 障害そのものを検出する API ではない。 */
    if (!g_backend || g_backend->bb_format != GFX_BB_PLANAR4 ||
        !bb[0] || !bb[1] || !bb[2] || !bb[3]) {
        gfx_shutdown();
        tvram_clear();
        return;
    }

    /* パレット設定 */
    palette_set(PAL_BG,         0,  0,  0);
    palette_set(PAL_GRAD,        8,  0,  8);
    palette_set(PAL_OUTLINE,      1,  1,  1);
    palette_set(PAL_FACE,        11, 11, 11);
    palette_set(PAL_SHADOW,       5,  5,  5);
    palette_set(PAL_HIGHLIGHT,   15, 15, 15);

    /* 全画面をグラデーションカラーで塗りつぶし */
    bb_clear(PAL_GRAD);

    /* 3D文字描画: O S 3 2 */
    cx = TEXT_X;
    for (i = 0; i < NUM_CHARS; i++) {
        draw_char_3d(cx, TEXT_Y, gdata[i], gcount[i]);
        cx += CHAR_W + CHAR_GAP;
    }

    /* グラデーションテーブル生成 */
    make_grad_table();

    /* 初回: VRAM転送 + ラスタパレット */
    grad_offset = 0;
    build_raster(&raster_table, grad_offset);
    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present_raster(&raster_table);

    /* アニメーションループ (3秒間) */
    start_tick = tick_count;
    while (tick_count - start_tick < BOOT_TIMEOUT) {
        grad_offset = (grad_offset + 1) % GRAD_STEPS;
        build_raster(&raster_table, grad_offset);
        gfx_present_raster(&raster_table);
    }

    /* クリーンアップ: VRAM全クリア → テキストモード復帰 */
    bb_clear(0);
    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present();
    gfx_shutdown();
    palette_init();
    tvram_clear();
}
