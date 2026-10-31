/* Screen capabilities, saved state, drawing counters and palette APIs. */
/* ======================================================================== */
/*  KAPI: 画面能力の問い合わせ (GUI HAL, docs/design/sdk/include/os32/os32_kapi_shared.md G5)  */
/*  現在の唯一のバックエンドは 9801 プレーン: CPU 直書き、HW 塗り/転送なし。   */
/* ======================================================================== */
int gfx_ver_state(GFX_ScreenInfo *out, u32 *kind, u32 *active)
{
    GFX_ScreenInfo info = {0};
    int rc;

    if (!out || !kind || !active) return OS32_ERR_INVAL;
    *out = info;
    *kind = GFX_VER_UNKNOWN;
    *active = 0;
    if (!g_backend || !g_backend->query) return OS32_ERR_NOSYS;
    rc = g_backend->query(&info);
    if (rc) return rc;

    const GfxMode *mode = gfx_backend_mode(g_backend);
    if (gfx_started) info.height = gfx_current_height;
    else if (mode) info.height = mode->height;
    if (g_backend == &gfx_backend_pc98) *kind = GFX_VER_PC98;
    else if (g_backend == &gfx_backend_pegc) *kind = GFX_VER_PEGC;
    else if (g_backend == &gfx_backend_cirrus) *kind = GFX_VER_CIRRUS;
    else if (g_backend == &gfx_backend_geforce) *kind = GFX_VER_GEFORCE;
    *active = gfx_started != 0;
    *out = info;
    return 0;
}

void __cdecl gfx_screen_info(void *out)
{
    GFX_ScreenInfo *info = out;
    if (!out) return;
    /* Flags and palette leases remain backend-owned. After shutdown the
     * default row describes the next mode, not the restored CUI height. */
    if (g_backend && g_backend->query && !g_backend->query(info)) {
        const GfxMode *mode = gfx_backend_mode(g_backend);
        if (mode) info->height = gfx_started ? gfx_current_height : mode->height;
    }
}

/* ======================================================================== */
/*  KAPI: gfx_set_palette — 1 色差し替え。HAL 経由 (レビュー ④)。            */
/*  従来は palette_set を直接叩いており、PEGC/Cirrus でも 9801 の 16 色       */
/*  palette_set へ行ってしまう問題があった。バックエンドの set_palette へ    */
/*  通し、機種ごとの実装に届くようにする。                                   */
/* ======================================================================== */
void __cdecl gfx_set_palette_hal(int idx, u8 r, u8 g, u8 b)
{
    u8 rgb[GFX_RGB_COMPONENTS];
    rgb[GFX_RGB_R] = r; rgb[GFX_RGB_G] = g; rgb[GFX_RGB_B] = b;
    if (g_backend && g_backend->set_palette) {
        g_backend->set_palette(idx, 1, rgb);
    } else {
        palette_set(idx, r, g, b);   /* 保険 */
    }
}

/* ======================================================================== */
/*  KAPI: ハードウェア塗り / 転送 (アクセラレータ系バックエンド用の枠)        */
/*  バックエンドが fill_rect / blit を持てばそれへ、無ければ OS32_ERR_NOSYS。 */
/*  呼ぶ側は GFX_CAP_HW_* を見て CPU 実装へフォールバックする。               */
/* ======================================================================== */
int __cdecl gfx_hw_fill_rect(int x, int y, int w, int h, u8 color)
{
    if (g_backend && g_backend->fill_rect)
        return g_backend->fill_rect(x, y, w, h, color);
    return OS32_ERR_NOSYS;
}

int __cdecl gfx_hw_blit(int dx, int dy, int sx, int sy, int w, int h)
{
    if (g_backend && g_backend->blit)
        return g_backend->blit(dx, dy, sx, sy, w, h);
    return OS32_ERR_NOSYS;
}

/* ======================================================================== */
/*  KAPI (v41): 描画カウンタの取得 (契約 G7 / DESIGN §8)。                   */
/*  GFX_Stats を埋める。K1 が kapi.json に載せる弱既定 (NOSYS 返し) を、この   */
/*  強シンボルがリンク時に上書きする。                                       */
/* ======================================================================== */
int __cdecl gfx_stats(void *out)
{
    GFX_Stats *s = (GFX_Stats *)out;
    if (!s) return OS32_ERR_INVAL;
    s->present_bytes = gfx_counters.present_bytes;
    s->hw_ops        = gfx_counters.hw_ops;
    s->io_accesses   = gfx_counters.io_accesses;
    s->commits       = gfx_counters.commits;
    return 0;
}

/* ======================================================================== */
/*  KAPI (v41): パレットのリース (契約 G8)。                                 */
/*  フォーカスのあるアプリ / WM が、バックエンドが許す範囲だけパレットを      */
/*  差し替える。範囲は gfx_screen_info() の lease_mask (16 色) /              */
/*  lease_first, lease_count (256 色) が正典。範囲外は OS32_ERR_INVAL で弾く。 */
/*  H1 は状態を持たない — システム色へ戻すのは WM が同じ関数で行う。          */
/* ======================================================================== */
int __cdecl gfx_lease_palette(int first, int count, const u8 *rgb)
{
    GFX_ScreenInfo si;
    int i;

    if (count <= 0 || first < 0 || !rgb) return OS32_ERR_INVAL;
    if (!g_backend || !g_backend->query || !g_backend->set_palette)
        return OS32_ERR_NOSYS;

    g_backend->query(&si);

    if (si.lease_count > 0) {
        /* 256 色機: 連続範囲 [lease_first, lease_first + lease_count) */
        if (first < (int)si.lease_first ||
            first > (int)si.lease_first + (int)si.lease_count ||
            count > (int)si.lease_first + (int)si.lease_count - first)
            return OS32_ERR_INVAL;
    } else {
        /* 16 色機: lease_mask のビットが立つ index だけ貸せる */
        if (first >= GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP) || count > GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP) - first) return OS32_ERR_INVAL;
        for (i = first; i < first + count; i++) {
            if (!(si.lease_mask & (u16)(1u << i))) return OS32_ERR_INVAL;
        }
    }

    g_backend->set_palette(first, count, rgb);
    return 0;
}

int gfx_get_height(void)
{
    return gfx_current_height;
}

