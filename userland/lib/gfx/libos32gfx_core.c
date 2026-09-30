#include "libos32gfx.h"

KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_dirty_suppress;

/* パックド 8bpp (PEGC 256 色 / Cirrus) モードか。0 = 4 プレーン (PC-9801 標準)。
 * libos32gfx_attach が決め、各描画関数はこれを見て経路を分ける。
 * 票 H2b / 契約 G5。 */
int gfx_packed;

/* ------------------------------------------------------------------------ */
/*  libos32gfx_attach — 既に GFX モードの画面へ「取り付く」                   */
/*                                                                          */
/*  gfx_init() は呼ばない。framebuffer 記述子を取り直し、画素形式を判定し、   */
/*  サーフェス / スプライトのプールを初期化する。                            */
/*  呼び手: libos32gfx_init (gfx_init の後)、libos32gui の attach_gfx         */
/*  (gshell 配下のアプリと共有ライブラリの shlib_init。gshell が既に GFX      */
/*  モードにしているので init を呼ぶとデスクトップを壊す)。                  */
/*  ⚠ 画素形式の判定をここに置くのは、attach 側で忘れると PACKED8 でも        */
/*  プレーン経路に落ちて漢字 (gfx_draw_font) が描けなくなるため              */
/*  (2026-09-06、G5 後半の gui_demo で実測。ANK は libos32gui が自前で置く    */
/*  ので気付きにくい)。冪等。                                                */
/* ------------------------------------------------------------------------ */
void libos32gfx_attach(KernelAPI *api)
{
    gfx_api = api;
    gfx_api->gfx_get_framebuffer(&gfx_fb);

    /* 画素形式の判定 (H2b)。
     * gfx_get_framebuffer は パックド系バックエンドでは planes[0] にだけ
     * バックバッファを入れ、planes[1..3] を NULL にする (gfx/gfx_core.c)。
     * これが一次判定。KAPI に gfx_screen_info があるビルドでは format も
     * 突き合わせ、どちらかが PACKED8 を示せばパックド経路にする。 */
    gfx_packed = (gfx_fb.planes[1] == (u8 *)0) ? 1 : 0;
    if (!gfx_packed && gfx_api->version >= 40) {
        GFX_ScreenInfo si;
        si.format = GFX_FMT_PLANAR4;
        gfx_api->gfx_screen_info(&si);
        if (si.format == GFX_FMT_PACKED8) gfx_packed = 1;
    }

    gfx_surface_init();
    gfx_sprite_init();
}

void libos32gfx_init(KernelAPI *api)
{
    gfx_api = api;
    gfx_api->gfx_init();
    libos32gfx_attach(api);
}

void libos32gfx_shutdown(void)
{
    gfx_api->gfx_shutdown();
}

void gfx_present(void)
{
    /* 画面全体が更新された場合 */
    gfx_api->gfx_add_dirty_rect(0, 0, gfx_fb.width, gfx_fb.height);
}
