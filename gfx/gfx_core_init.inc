/* 400/200 行の初期化。packed 系はどちらの入口でも native 解像度を使う。 */
static void gfx_init_mode(int legacy200)
{
    pc98_splash_raster_reset();
    gfx_init_gen++;
    /* GDC 初期化より前にバックエンドを決める。 */
    int initialized = gfx_select_and_init_backend(legacy200);
    if (initialized < 0) return;
    if (initialized) {
        gfx_reinit_surfaces(1);
        gfx_started = bb[0] != 0;
        if (g_backend->enter) g_backend->enter();
        gfx_diag_init();
        return;   /* packed 系はモード設定もバックバッファも init() が済ませた */
    }

    int plane_sz = legacy200 ? GFX_PLANE_SZ_200 : GFX_PLANE_SZ;
    gfx_current_height = gfx_backend_mode(g_backend)->height;
    /* 400 行では bind 前、200 行ではページ設定後にリセットしていた順を保つ。 */
    if (!legacy200) {
        gfx_display_page = 0;
        prev_dirty.count = 0;
    }

    gfx_bind_client(); /* retain 400-line plane stride in both modes */
    if (!bb[0]) { gfx_reinit_surfaces(1); gfx_diag_init(); return; }
    _gfx_common_init(plane_sz);

    /* GDC CSRFORM: L/R=1 のとき各ラインを2倍表示する。 */
    _out(GDC_GFX_CMD, GDC_GFX_400LINE);
    _out(GDC_GFX_PARAM, legacy200 ? 0x01 : 0x00);
    _out(MODE_FF1_PORT, legacy200 ? MFF1_200LINE : MFF1_HIRES);

    _out(GDC_GFX_CMD, GDC_CMD_START);

    gfx_clear_planar_pages(plane_sz);

    /* ページ0を表示、ページ1に描画 */
    _out(GDC_DISP_PAGE, GDC_PAGE_0);
    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);
    gfx_flip_enabled = 1;
    if (legacy200) {
        gfx_display_page = 0;
        prev_dirty.count = 0;
    }

    palette_init();
    gfx_scroll_init();
    gfx_reinit_surfaces(1);
    gfx_started = 1;

    /* 表示出力を有効化 (9801 の enter は空)。 */
    if (g_backend && g_backend->enter) g_backend->enter();
    gfx_diag_init();
}
