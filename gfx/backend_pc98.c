/* ======================================================================== */
/*  BACKEND_PC98.C — 9801 プレーンバックエンド (GfxBackend 実装, レーン H1)   */
/*                                                                          */
/*  PC-9801 標準グラフィック (640x400/200, 16 色, 4 プレーン) を GfxBackend   */
/*  として提供する。実際の VRAM 転送エンジン (ダーティキュー / ページフリップ) */
/*  は gfx_vram.c にあり、本ファイルはその上に HAL の記述子と能力申告・        */
/*  カウンタ加算を載せる薄い層。                                             */
/*                                                                          */
/*  [HW1] CPU 直書き: 全画素は CPU から 0xA8000〜 の VRAM へ直接書く。         */
/*  アクセラレータ枠 (fill_rect / blit) は NULL のままで、呼び出し側が CPU     */
/*  共通実装へフォールバックする (DESIGN §7-1)。                              */
/*                                                                          */
/*  VRAM プレーンアドレス / パレット I/O の出典は gfx.h (PC9800Bible §2-7)。   */
/*  9821 の PEGC / Cirrus バックエンドは backend_pegc.c / backend_wab.c として */
/*  この表にもう 1 枚ずつ足す (H2 / H3)。                                     */
/* ======================================================================== */

#include "gfx_internal.h"   /* gfx.h, pc98.h, palette.h, 内部変数, _out 等 */
#include "gfx_hal.h"
#include "os32_kapi_shared.h"

/* gfx_vram.c の VRAM 転送エンジン (低レベル present)。
 * バックエンドはこれを呼び、gfx_present_rect / gfx_present は本バックエンドの
 * present_rect を経由する (再帰しない)。 */
extern void __cdecl gfx_add_dirty_rect(int x, int y, int w, int h);
extern void __cdecl gfx_present_dirty(void);

/* ------------------------------------------------------------------------ */
/*  probe: 9801 標準グラフィックは常に使える (最後のフォールバック)           */
/* ------------------------------------------------------------------------ */
static int pc98_probe(void)
{
    return 1;
}

/* ------------------------------------------------------------------------ */
/*  init: 能力ビットと画面情報を埋める (契約 G5)。                            */
/*  ハードウェアは触らない (モード設定は gfx_init / gfx_init_200 が行う)。     */
/*  現在のモード (400/200 ライン, フリップ有無) を正直に申告する — GUI が      */
/*  gfx_screen_info() を信じられるのはここが決め打ちしないから (DESIGN §7-2)。 */
/* ------------------------------------------------------------------------ */
static int pc98_query(GFX_ScreenInfo *info)
{
    int i;
    if (!info) return OS32_ERR_INVAL;

    info->width  = (u16)GFX_WIDTH;
    info->height = (u16)gfx_current_height;      /* 400 or 200 */
    info->bpp    = 4;
    info->format = GFX_FMT_PLANAR4;
    info->flags  = GFX_CAP_TEXT_OVERLAY |
                   (gfx_flip_enabled ? GFX_CAP_PAGE_FLIP : 0);
    /* パレットのリース (G8): 16 色機は index 1-6, 8-15 の 14 項目を貸せる。
     * 不可侵は 0 (TEXT 黒) と 7 (WINDOW 白)。lease_first/count は 256 色機用。 */
    info->lease_mask  = 0xFF7E;   /* index 1-6 と 8-15 の 14 色 (契約 G8)。0 と 7 のみ不可侵 */
    info->lease_first = 0;
    info->lease_count = 0;
    for (i = 0; i < 5; i++) info->reserved[i] = 0;
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  shutdown: フリップ解除 → ページ 0 復帰 → GDC 表示停止。                   */
/*  (旧 gfx_shutdown の本体そのまま。回帰なし)                                */
/* ------------------------------------------------------------------------ */
static void pc98_shutdown(void)
{
    if (gfx_flip_enabled) {
        _out(GDC_DISP_PAGE, 0x00);
        _out(GDC_ACCESS_PAGE, 0x00);
        gfx_flip_enabled = 0;
        gfx_display_page = 0;
    }
    gfx_current_height = GFX_HEIGHT;
    _out(GDC_GFX_CMD, GDC_CMD_STOP);
}

/* ------------------------------------------------------------------------ */
/*  present で書き込む VRAM バイト数を数える (契約 G7 / DESIGN §8)。          */
/*  gfx_add_dirty_rect と同じ 32px アライン + 画面クリップで、要求された矩形の */
/*  論理 present サイズ ((aligned_w/8) × h × 4 プレーン) を加算する。         */
/*  gfx_present() 1 回 = 640/8×400×4 = 128000、present_rect(0,0,32,16) =      */
/*  4×16×4 = 256 (完了条件)。フリップのステイルページ再転送は含めない         */
/*  (論理 present 量を数えるため)。                                          */
/* ------------------------------------------------------------------------ */
static void pc98_count_present(int x, int y, int w, int h)
{
    int right = x + w;
    int ax = x & ~31;
    int aw = ((right + 31) & ~31) - ax;

    if (ax < 0) { aw += ax; ax = 0; }
    if (y < 0) { h += y; y = 0; }
    if (ax + aw > GFX_WIDTH) aw = GFX_WIDTH - ax;
    if (y + h > gfx_current_height) h = gfx_current_height - y;
    if (aw <= 0 || h <= 0) return;

    /* aw は 32 の倍数なので aw/8 は偶数 → 実転送ワード×2 と一致する */
    /* present_bytes は実 VRAM 転送点 (_flush_dirty_queue) で数える (レビュー ⑦)。
     * ここで論理 damage を数えると、ステイルページ再転送分が漏れる。 */
}

/* ------------------------------------------------------------------------ */
/*  present_rect: バックバッファの矩形を VRAM へ転送する。                    */
/*  commit カウンタと io_accesses (ページ切替 OUT) は gfx_present_dirty /     */
/*  _flip_page 側で加算する。                                                */
/* ------------------------------------------------------------------------ */
static void pc98_present_rect(int x, int y, int w, int h)
{
    pc98_count_present(x, y, w, h);
    gfx_add_dirty_rect(x, y, w, h);
    gfx_present_dirty();
}

/* ------------------------------------------------------------------------ */
/*  set_palette: 連続する count 項目を差し替える。rgb は 3B/項目 (R,G,B)。    */
/*  16 色機の輝度は 0-15 (palette_set が下位 4bit にマスク)。                 */
/* ------------------------------------------------------------------------ */
static void pc98_set_palette(int first, int count, const u8 *rgb)
{
    int k;
    if (!rgb) return;
    for (k = 0; k < count; k++) {
        int idx = first + k;
        if (idx < 0 || idx >= PALETTE_COUNT) continue;
        palette_set(idx, rgb[k * 3 + 0], rgb[k * 3 + 1], rgb[k * 3 + 2]);
    }
    /* パレット 1 項目あたり OUT 4 本 (番号 + G/R/B)。 */
    if (count > 0) gfx_counters.io_accesses += (u32)count * 4u;
}

/* ------------------------------------------------------------------------ */
/*  enter / leave: 9801 標準グラフィックには表示出力の切替 (リレー) が無い。   */
/*  アクセラレータ系 (Cirrus) だけがここでリレーを切り替える (DESIGN §8)。     */
/* ------------------------------------------------------------------------ */
static void pc98_enter(void) { }
static void pc98_leave(void) { }

/* ------------------------------------------------------------------------ */
/*  バックエンド表。fill_rect / blit は NULL = CPU 共通実装へフォールバック。 */
/* ------------------------------------------------------------------------ */
const GfxBackend gfx_backend_pc98 = {
    "pc98-planar",
    pc98_probe,
    (void (*)(void))0,   /* init: 9801 は GDC/プレーンを gfx_init が済ませるので別 hw-init 不要 */
    pc98_query,
    pc98_shutdown,
    pc98_present_rect,
    pc98_set_palette,
    pc98_enter,
    pc98_leave,
    (int (*)(int, int, int, int, u8))0,        /* fill_rect: CPU 実装へ */
    (int (*)(int, int, int, int, int, int))0,  /* blit:      CPU 実装へ */
    (u8 *)MEM_GFX_BB_BASE,   /* bb_base: プレーン 0 (青) 先頭 */
    (u32)GFX_BPL,            /* bb_pitch: 80 バイト/ライン */
    GFX_BB_PLANAR4,
    (u32)MEM_GFX_BB_SIZE,    /* bb_size: 4 プレーン分 128KB (0x6A000-0x89FFF) */
    (void (*)(void))0        /* prepare: 9801 は起動時の下ごしらえが無い */
};
