/* Screen capabilities, rectangle presentation and palette updates. */
/* ------------------------------------------------------------------------ */
/*  query — 能力ビットと画面情報 (契約 G5)。副作用なし・冪等。               */
/* ------------------------------------------------------------------------ */
static int pegc_query(GFX_ScreenInfo *info)
{
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_pegc);
    GfxFormatDesc desc;
    int i;
    if (!info || !mode || !gfx_format_desc(mode->format, &desc)) return OS32_ERR_INVAL;

    info->width  = mode->width;
    info->height = (u16)PEGC_HEIGHT_480;
    info->bpp    = desc.bpp;
    info->format = mode->format;

    /* 能力ビット (票 7):
     *  TEXT_OVERLAY = 1。NP21/W で実測 (2026-09-06): 256 色モード中に TVRAM へ
     *    書いた文字 (gui_busy の kprintf) が 256 色の絵の上に合成されて見えた
     *    (DESIGN §5 に書き戻し済み)。R4 の TVRAM クロームは PEGC でも使える。
     *  PAGE_FLIP    = 0。800 ライン VRAM 構成では 00A4h (表示ページ選択) が
     *    使えないので、PEGC のページ切替は v1 では使わない。
     *  HW_FILL/HW_BLT = 0。PEGC は塗り/転送エンジンを持たない (CPU 直書き)。 */
    info->flags  = GFX_CAP_TEXT_OVERLAY;

    /* パレットのリース (契約 G8): 256 色機は連続範囲で申告する。
     * lease_mask は 16 色機用なので 0。 */
    info->lease_mask  = 0;
    info->lease_first = (u16)PEGC_LEASE_FIRST;
    info->lease_count = (u16)PEGC_LEASE_COUNT;
    for (i = 0; i < (int)(sizeof(info->reserved) / sizeof(info->reserved[0])); i++) info->reserved[i] = 0;
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  present_rect — バックバッファの矩形をリニア窓へ転送                      */
/*                                                                          */
/*  1 ライン 640 バイトの単純な矩形コピー。プレーンに分解しないので 9801 の   */
/*  ダーティキュー (gfx_vram.c) は通さず、ここで直接転送する。               */
/*  ⚠ 資料 ([B] §2-7「VRAM アクセスの注意点」):「32 ビットマシンであっても    */
/*  VRAM は 16 ビットバス接続であるため、32 ビットアクセスをしても 16 ビット  */
/*  2 回分の時間がかかります」。rep movsd はバス時間を縮めないが、発行する    */
/*  命令数は半分になる (票 5)。                                              */
/*  カウンタは 1 バイト/画素で加算する = 全画面で 640*480 = 307200 (完了条件)。*/
/* ------------------------------------------------------------------------ */
static void pegc_present_rect(int x, int y, int w, int h)
{
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_pegc);
    u8 *src;
    u8 *dst;
    int row;
    u32 bytes;

    if (!s_active || s_bb_phys == 0) return;

    if (!gfx_clip_screen(&x, &y, &w, &h, mode->width, mode->height)) return;

    src = (u8 *)P2V(s_bb_phys) + (u32)y * mode->pitch + (u32)x;
    dst = (u8 *)P2V_IO(s_window_base) + (u32)y * mode->pitch + (u32)x;
    bytes = (u32)w;

    for (row = 0; row < h; row++) {
        /* 先頭の端数 → dword 単位 → 末尾の端数。x と w が 4 の倍数なら
         * 端数ループは 0 回で、丸ごと rep movsd になる。 */
        u32 head = ((sizeof(u32) - ((u32)dst & (sizeof(u32) - 1))) & (sizeof(u32) - 1));
        u32 mid;
        u32 tail;
        if (head > bytes) head = bytes;
        mid  = (bytes - head) >> COPY_WORD_SHIFT;
        tail = (bytes - head) - (mid << COPY_WORD_SHIFT);

        if (head) kmemcpy(dst, src, head);
        if (mid)  _memcpy_d(dst + head, src + head, mid);
        if (tail) kmemcpy(dst + head + (mid << COPY_WORD_SHIFT), src + head + (mid << COPY_WORD_SHIFT),
                          tail);

        src += mode->pitch;
        dst += mode->pitch;
    }

    gfx_counters.present_bytes += bytes * (u32)h;   /* 1 バイト/画素 */
    gfx_counters.commits++;
}

/* ------------------------------------------------------------------------ */
/*  set_palette — 連続する count 項目を差し替える (rgb は 3B/項目)           */
/*  ハードウェアの並びは G, R, B ([U] パレットレジスタ) だが、HAL の引数は    */
/*  9801 と同じ R, G, B 順。                                                 */
/*                                                                          */
/*  引数の輝度は **KAPI のスケール = 0〜15** (契約 G6/G8 の GuiRgb)。         */
/*  gfx_set_palette (9801 互換の 1 色差し替え) も gfx_lease_palette (G8) も   */
/*  この単位で来るので、ここで一括して 8bit へ伸ばす (票 H2c)。               */
/*  0〜15 のシステム色はシャドウ台帳にも書き戻し、gfx_get_palette            */
/*  (= palette_get) が実際の色を返せるようにしておく。                        */
/* ------------------------------------------------------------------------ */
static void pegc_set_palette(int first, int count, const u8 *rgb)
{
    int k;
    if (!rgb) return;
    for (k = 0; k < count; k++) {
        int idx = first + k;
        u8 r = rgb[k * GFX_RGB_COMPONENTS + GFX_RGB_R];
        u8 g = rgb[k * GFX_RGB_COMPONENTS + GFX_RGB_G];
        u8 b = rgb[k * GFX_RGB_COMPONENTS + GFX_RGB_B];
        if (idx < 0 || idx >= GFX_PAL_ENTRIES(GFX_FMT_PACKED8_BPP)) continue;
        pegc_write_palette(idx, pegc_pal8(r), pegc_pal8(g), pegc_pal8(b));
        if (idx < GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP)) palette_shadow_set(idx, r, g, b);
    }
}

