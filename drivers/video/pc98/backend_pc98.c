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

#include "memmap.h"
#include "../../../gfx/gfx_internal.h"   /* gfx.h, pc98.h, palette.h, 内部変数, _out 等 */
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
#define PC98_LEASE_MASK 0xFF7E
#define PC98_PALETTE_IO_WRITES 4u
#define PC98_DAC_BITS 4
#define PC98_MODE_400 0
#define PC98_MODE_200 1

static const GfxMode pc98_modes[] = {
    {GFX_WIDTH, GFX_HEIGHT, GFX_FMT_PLANAR4, PC98_DAC_BITS, PC98_MODE_400, 0, GFX_BPL},
    {GFX_WIDTH, GFX_HEIGHT_200, GFX_FMT_PLANAR4, PC98_DAC_BITS, PC98_MODE_200, 0, GFX_BPL}
};

static int pc98_query(GFX_ScreenInfo *info)
{
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_pc98);
    GfxFormatDesc desc;
    int i;
    if (!info || !mode || !gfx_format_desc(mode->format, &desc)) return OS32_ERR_INVAL;

    info->width  = mode->width;
    info->height = (u16)gfx_current_height;      /* 400 or 200 */
    info->bpp    = desc.bpp;
    info->format = mode->format;
    info->flags  = GFX_CAP_TEXT_OVERLAY |
                   (gfx_flip_enabled ? GFX_CAP_PAGE_FLIP : 0);
    /* パレットのリース (G8): 16 色機は index 1-6, 8-15 の 14 項目を貸せる。
     * 不可侵は 0 (TEXT 黒) と 7 (WINDOW 白)。lease_first/count は 256 色機用。 */
    info->lease_mask  = PC98_LEASE_MASK;   /* index 1-6 と 8-15 の 14 色 (契約 G8)。0 と 7 のみ不可侵 */
    info->lease_first = 0;
    info->lease_count = 0;
    for (i = 0; i < (int)(sizeof(info->reserved) / sizeof(info->reserved[0])); i++) info->reserved[i] = 0;
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  shutdown: フリップ解除 → ページ 0 復帰 → GDC 表示停止。                   */
/*  (旧 gfx_shutdown の本体そのまま。回帰なし)                                */
/* ------------------------------------------------------------------------ */
static void pc98_shutdown(void)
{
    if (gfx_flip_enabled) {
        _out(GDC_DISP_PAGE, GDC_PAGE_0);
        _out(GDC_ACCESS_PAGE, GDC_PAGE_0);
        gfx_flip_enabled = 0;
        gfx_display_page = 0;
    }
    gfx_backend_pc98.selected_mode = gfx_mode_pick(&gfx_backend_pc98, 0);
    gfx_current_height = gfx_backend_mode(&gfx_backend_pc98)->height;
    _out(GDC_GFX_CMD, GDC_CMD_STOP);
}

/* ------------------------------------------------------------------------ */
/*  present_rect: バックバッファの矩形を VRAM へ転送する。                    */
/*  commit カウンタと io_accesses (ページ切替 OUT) は gfx_present_dirty /     */
/*  _flip_page 側で加算する。                                                */
/* ------------------------------------------------------------------------ */
static void pc98_present_rect(int x, int y, int w, int h)
{
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
        if (idx < 0 || idx >= GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP)) continue;
        palette_set(idx, rgb[k * GFX_RGB_COMPONENTS + GFX_RGB_R], rgb[k * GFX_RGB_COMPONENTS + GFX_RGB_G], rgb[k * GFX_RGB_COMPONENTS + GFX_RGB_B]);
    }
    /* パレット 1 項目あたり OUT 4 本 (番号 + G/R/B)。 */
    if (count > 0) gfx_counters.io_accesses += (u32)count * PC98_PALETTE_IO_WRITES;
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
GfxBackend gfx_backend_pc98 = {
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
    (u8 *)0,                /* bound from the registered CLIENT */
    (u32)GFX_BPL,            /* bb_pitch: 80 バイト/ライン */
    GFX_BB_PLANAR4,
    (u32)MEM_GFX_BB_SIZE,    /* bb_size: 4 プレーン分 128KB (boot pool CLIENT) */
    (void (*)(void))0,       /* prepare: 9801 は起動時の下ごしらえが無い */
    pc98_modes,
    sizeof(pc98_modes) / sizeof(pc98_modes[0]),
    PC98_MODE_400,           /* default_mode */
    (const GfxMode *)0       /* selected_mode */
};
