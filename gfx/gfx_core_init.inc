/* 400/200 行の初期化。packed 系はどちらの入口でも native 解像度を使う。 */
static void gfx_init_mode(int legacy200)
{
    palette_shadow_reset();
    gfx2_pal_reset16();
    pc98_splash_raster_reset();
    gfx_init_gen++;
    /* GDC 初期化より前にバックエンドを決める。 */
    int initialized = gfx_select_and_init_backend(legacy200);
    if (initialized < 0) return;
    gfx2_register_selected();
    if (initialized) {
        gfx_reinit_surfaces(1);
        gfx_started = bb[0] != 0;
        if (g_backend->enter) g_backend->enter();
        if (gfx_started && display_dual_dpms) display_dual_dpms(0);
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

    /* 200/400 行の表示を始める (PC-98 は GDC の CSRFORM・モード F/F1・START)。 */
    display_planar_start(legacy200);

    gfx_clear_planar_pages(plane_sz);

    /* ページ0を表示、ページ1に描画 */
    display_flip_pages(0, 1);
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
